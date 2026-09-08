#include "audio/audio_coordinator.hpp"
#include "pipeline/client_transport_presentation.hpp"
#include "audio/audio_engine.hpp"
#include "audio/music_manager.hpp"
#include "audio/footstep_manager.hpp"
#include "audio/activity_sound_manager.hpp"
#include "audio/mount_sound_manager.hpp"
#include "audio/npc_voice_manager.hpp"
#include "audio/player_voice_manager.hpp"
#include "audio/ambient_sound_manager.hpp"
#include "audio/ui_sound_manager.hpp"
#include "audio/combat_sound_manager.hpp"
#include "audio/spell_sound_manager.hpp"
#include "audio/movement_sound_manager.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/dbc_loader.hpp"
#include <algorithm>
#include <cctype>
#include "game/zone_manager.hpp"
#include "core/logger.hpp"

namespace wowee {
namespace audio {

AudioCoordinator::AudioCoordinator() = default;

AudioCoordinator::~AudioCoordinator() {
    shutdown();
}

bool AudioCoordinator::initialize() {
    // Initialize AudioEngine (singleton)
    if (!AudioEngine::instance().initialize()) {
        LOG_WARNING("Failed to initialize AudioEngine - audio will be disabled");
        audioAvailable_ = false;
        return false;
    }
    audioAvailable_ = true;

    // Create all audio managers (initialized later with asset manager)
    musicManager_ = std::make_unique<MusicManager>();
    footstepManager_ = std::make_unique<FootstepManager>();
    activitySoundManager_ = std::make_unique<ActivitySoundManager>();
    mountSoundManager_ = std::make_unique<MountSoundManager>();
    npcVoiceManager_ = std::make_unique<NpcVoiceManager>();
    playerVoiceManager_ = std::make_unique<PlayerVoiceManager>();
    ambientSoundManager_ = std::make_unique<AmbientSoundManager>();
    uiSoundManager_ = std::make_unique<UiSoundManager>();
    combatSoundManager_ = std::make_unique<CombatSoundManager>();
    spellSoundManager_ = std::make_unique<SpellSoundManager>();
    movementSoundManager_ = std::make_unique<MovementSoundManager>();

    LOG_INFO("AudioCoordinator initialized with ", 11, " audio managers");
    return true;
}
void AudioCoordinator::shutdown() {
    stopWorldAudio();
    // Reset all managers first (they may reference AudioEngine)
    movementSoundManager_.reset();
    spellSoundManager_.reset();
    combatSoundManager_.reset();
    uiSoundManager_.reset();
    ambientSoundManager_.reset();
    playerVoiceManager_.reset();
    npcVoiceManager_.reset();
    mountSoundManager_.reset();
    activitySoundManager_.reset();
    footstepManager_.reset();
    musicManager_.reset();

    // Shutdown audio engine last
    if (audioAvailable_) {
        AudioEngine::instance().shutdown();
        audioAvailable_ = false;
    }

    LOG_INFO("AudioCoordinator shutdown complete");
}

void AudioCoordinator::stopWorldAudio() {
    worldAudioActive_ = false;
    musicTransport_=0;transportMusicPath_.clear();
    if (musicManager_) {
        musicManager_->resetWorldState(); // cancels load/crossfade, releases zone tracks
    }
    if (spellSoundManager_) spellSoundManager_->stopPrecast();
    if (ambientSoundManager_) ambientSoundManager_->resetWorldState();
    if (activitySoundManager_) activitySoundManager_->resetWorldState();
    if (mountSoundManager_) mountSoundManager_->resetWorldState();
    if (npcVoiceManager_) npcVoiceManager_->resetWorldState();
    if (footstepManager_) footstepManager_->resetWorldState();
    if (playerVoiceManager_) playerVoiceManager_->resetWorldState();
    AudioEngine::instance().stopNarration();
    AudioEngine::instance().stopAllSounds();
    currentZoneId_ = 0;
    playingMusicArea_ = 0; playingMusicPeriod_ = -1; musicTimeHours_ = 12;
    currentZoneName_.clear();
    inTavern_ = inBlacksmith_ = false;
    musicSwitchCooldown_ = 0.0f;
}

void AudioCoordinator::setTransportMusic(uint32_t entry,bool zeppelin,pipeline::AssetManager* assets) {
    const unsigned family = zeppelin ? 1u : 0u;
    const auto now = std::chrono::steady_clock::now();
    if (musicTransport_ == entry && (!entry || !transportMusicPath_.empty() ||
        transportTrackScanned_[family] || now < transportMusicRetryAt_)) return;
    transportMusicRetryAt_ = now + std::chrono::seconds(2);
    musicTransport_=entry;transportMusicPath_.clear();
    if(entry && assets) {
        if(!transportTrackScanned_[family]) {
            auto dbc=assets->loadDBC("SoundEntries.dbc");
            if(dbc && dbc->isLoaded() && dbc->getFieldCount()>=29) {
                int best=0;
                for(uint32_t row=0;row<dbc->getRecordCount();++row) {
                    for(unsigned f=0;f<10;++f) {
                        std::string path=pipeline::clientSoundEntryPath(dbc.get(),row,f);
                        if(path.empty())continue;
                        std::string key=dbc->getString(row,2)+" "+path;
                        std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return char(std::tolower(c));});
                        if(key.find("music")==std::string::npos)continue;
                        int score=0;
                        if(zeppelin && (key.find("zeppelin")!=std::string::npos || key.find("zepplin")!=std::string::npos))score=100;
                        if(!zeppelin && (key.find("boat")!=std::string::npos || key.find("ship")!=std::string::npos))score=80;
                        if(key.find("transport")!=std::string::npos)score=std::max(score,30);
                        if(key.find("gunship")!=std::string::npos)score=0;
                        if(score>best && assets->fileExists(path)) {best=score;transportTracks_[family]=std::move(path);}
                    }
                }
                transportTrackScanned_[family]=true;
                LOG_INFO("[TRANSPORT_MUSIC] family=",zeppelin?"zeppelin":"ship", " client track=",
                    transportTracks_[family].empty()?"<not present in client metadata>":transportTracks_[family]);
            }
        }
        transportMusicPath_=transportTracks_[family];
    }
    // A single music voice owns the scene: transport while aboard, then the
    // destination area's playlist. Cinematic exclusivity gates the final mixer.
    if(musicManager_)musicManager_->stopMusic(0);
    if(!transportMusicPath_.empty() && musicManager_)musicManager_->playMusic(transportMusicPath_,true,250);
    playingMusicArea_=0;playingMusicPeriod_=-1;musicSwitchCooldown_=0;
    inTavern_=inBlacksmith_=false;
}

void AudioCoordinator::beginWorldAudio() {
    worldAudioActive_ = true;
}

void AudioCoordinator::playZoneMusic(const std::string& music) {
    if (music.empty() || !musicManager_) return;
    if (music.rfind("file:", 0) == 0) {
        musicManager_->crossfadeToFile(music.substr(5));
    } else {
        musicManager_->crossfadeTo(music);
    }
}

void AudioCoordinator::setZoneMusicLooping(bool loop) {
    if (musicManager_) musicManager_->setLooping(loop);
}

void AudioCoordinator::onOriginalSoundtrackDisabled(game::ZoneManager* zm) {
    if (!zm || !musicManager_) return;
    if (!musicManager_->isCurrentTrackFile()) return;
    if (!musicManager_->isPlaying() && !musicManager_->isLoading()) return;
    // Only act in-world with a known zone; the login screen intentionally
    // plays a file track and is not part of the zone rotation this setting
    // controls.
    if (currentZoneId_ == 0) return;

    std::string music = zm->getRandomMusic(currentZoneId_, musicTimeHours_);
    if (!music.empty() && music.rfind("file:", 0) != 0) {
        playZoneMusic(music);
        musicSwitchCooldown_ = 6.0f;
    } else {
        musicManager_->stopMusic();
    }
}

void AudioCoordinator::updateZoneAudio(const ZoneAudioContext& ctx) {
    if (!worldAudioActive_) return;
    float deltaTime = ctx.deltaTime;
    musicTimeHours_ = ctx.gameTimeHours;
    if (musicSwitchCooldown_ > 0.0f) {
        musicSwitchCooldown_ = std::max(0.0f, musicSwitchCooldown_ - deltaTime);
    }

    // Resolve the spatial zone before updating ambience. Zone ambience used to
    // run first, leaving it one zone behind and permanently on its noon default.
    auto* zm = ctx.zoneManager;
    const uint32_t tileZoneId = (zm && ctx.hasTile)
        ? zm->getZoneId(ctx.tileX, ctx.tileY)
        : 0;
    const uint32_t serverZoneId = (zm && ctx.serverZoneId != 0)
        ? zm->resolveAreaZoneId(ctx.serverZoneId)
        : ctx.serverZoneId;
    uint32_t zoneId = serverZoneId != 0 ? serverZoneId : tileZoneId;

    // ── Ambient weather audio sync ──
    if (ambientSoundManager_) {
        bool isBlacksmith = (ctx.insideWmoId == 96048);

        if (zoneId != 0) {
            ambientSoundManager_->setZoneId(zoneId);
        }
        ambientSoundManager_->setGameTime(ctx.gameTimeHours);

        // Map visual weather type to ambient sound weather type
        AmbientSoundManager::WeatherType audioWeatherType = AmbientSoundManager::WeatherType::NONE;
        if (ctx.weatherType == 1) { // RAIN
            if (ctx.weatherIntensity < 0.33f)      audioWeatherType = AmbientSoundManager::WeatherType::RAIN_LIGHT;
            else if (ctx.weatherIntensity < 0.66f)  audioWeatherType = AmbientSoundManager::WeatherType::RAIN_MEDIUM;
            else                                     audioWeatherType = AmbientSoundManager::WeatherType::RAIN_HEAVY;
        } else if (ctx.weatherType == 2) { // SNOW
            if (ctx.weatherIntensity < 0.33f)      audioWeatherType = AmbientSoundManager::WeatherType::SNOW_LIGHT;
            else if (ctx.weatherIntensity < 0.66f)  audioWeatherType = AmbientSoundManager::WeatherType::SNOW_MEDIUM;
            else                                     audioWeatherType = AmbientSoundManager::WeatherType::SNOW_HEAVY;
        }
        ambientSoundManager_->setWeather(audioWeatherType);
        ambientSoundManager_->update(deltaTime, ctx.cameraPosition, ctx.insideWmo, ctx.isSwimming, isBlacksmith);
    }

    if(musicTransport_ && !transportMusicPath_.empty() && musicManager_) {
        musicManager_->update(deltaTime);
        return;
    }
    // ── Zone detection and music transitions ──
    if (!zm || !musicManager_) return;
    // Keep subzone music distinct from the parent used for environmental ambience.
    uint32_t musicAreaId = ctx.serverZoneId ? ctx.serverZoneId : zoneId;

    bool insideTavern = false;
    bool insideBlacksmith = false;
    std::string tavernMusic;

    // WMO-based location overrides (taverns, blacksmiths, city zones)
    if (ctx.insideWmo) {
        uint32_t wmoModelId = ctx.insideWmoId;

        // Stormwind WMO → force Stormwind City zone
        if (wmoModelId == 10047) { zoneId = 1519; if(!musicAreaId) musicAreaId=1519; }

        // Log WMO transitions
        static uint32_t lastLoggedWmoId = 0;
        if (wmoModelId != lastLoggedWmoId) {
            LOG_INFO("Inside WMO model ID: ", wmoModelId);
            lastLoggedWmoId = wmoModelId;
        }

        // Blacksmith detection (ambient forge sounds)
        if (wmoModelId == 96048) {
            // Forge ambience does not replace the area music.
            insideBlacksmith = false;
        }

        // Tavern / inn detection
        if (wmoModelId == 191 || wmoModelId == 71414 || wmoModelId == 190 ||
            wmoModelId == 220 || wmoModelId == 221 ||
            wmoModelId == 5392 || wmoModelId == 5393) {
            insideTavern = true;
            static const std::vector<std::string> tavernTracks = {
                "Sound\\Music\\ZoneMusic\\TavernAlliance\\TavernAlliance01.mp3",
                "Sound\\Music\\ZoneMusic\\TavernAlliance\\TavernAlliance02.mp3",
                "Sound\\Music\\ZoneMusic\\TavernHuman\\RA_HumanTavern1A.mp3",
                "Sound\\Music\\ZoneMusic\\TavernHuman\\RA_HumanTavern2A.mp3",
            };
            static int tavernTrackIndex = 0;
            if(!inTavern_) {
                // Use a dedicated authored subzone when available; otherwise this
                // known human-inn model uses its original Alliance tavern tracks.
                const auto* area=zm->getZoneInfo(musicAreaId);
                tavernMusic = area && area->hasDbcMusic && musicAreaId != zoneId
                    ? zm->getRandomMusic(musicAreaId,ctx.gameTimeHours)
                    : tavernTracks[tavernTrackIndex++ % tavernTracks.size()];
                LOG_INFO("Entered tavern WMO ",wmoModelId,", track=",tavernMusic);
            }
        }
    }

    // Tavern music transitions
    if (insideTavern) {
        if (!inTavern_ && !tavernMusic.empty()) {
            inTavern_ = true;
            LOG_INFO("Entered tavern");
            musicManager_->playMusic(tavernMusic, true);
            musicSwitchCooldown_ = 6.0f;
        }
    } else if (inTavern_) {
        inTavern_ = false;
        LOG_INFO("Exited tavern");
        playingMusicArea_=0; playingMusicPeriod_=-1;
        auto* info = zm->getZoneInfo(currentZoneId_);
        if (info) {
            std::string music = zm->getRandomMusic(currentZoneId_, musicTimeHours_);
            if (!music.empty()) {
                playZoneMusic(music);
                musicSwitchCooldown_ = 6.0f;
            }
        }
    }

    // Blacksmith transitions (stop music, let ambience play)
    if (insideBlacksmith) {
        if (!inBlacksmith_) {
            inBlacksmith_ = true;
            LOG_INFO("Entered blacksmith - stopping music");
            musicManager_->stopMusic();
        }
    } else if (inBlacksmith_) {
        inBlacksmith_ = false;
        LOG_INFO("Exited blacksmith - restoring music");
        auto* info = zm->getZoneInfo(currentZoneId_);
        if (info) {
            std::string music = zm->getRandomMusic(currentZoneId_, musicTimeHours_);
            if (!music.empty()) {
                playZoneMusic(music);
                musicSwitchCooldown_ = 6.0f;
            }
        }
    }

    // Distinguish the last displayed zone from the last STARTED playlist. A
    // transition during the cooldown remains pending instead of being forgotten.
    if(zoneId && zoneId!=currentZoneId_) {
        currentZoneId_=zoneId;
        const auto* info=zm->getZoneInfo(zoneId);
        currentZoneName_=info?info->name:std::string{};
        LOG_INFO("Entered zone: ",currentZoneName_," area=",musicAreaId);
    }
    const auto musicZone=zm->resolveMusicZoneId(musicAreaId,ctx.gameTimeHours);
    const int period=game::ZoneManager::isMusicNight(ctx.gameTimeHours)?1:0;
    if(!insideTavern && !insideBlacksmith && musicSwitchCooldown_<=0 &&
       (musicZone!=playingMusicArea_ || period!=playingMusicPeriod_)) {
        const auto music=zm->getRandomMusic(musicAreaId,ctx.gameTimeHours);
        if(!music.empty()) playZoneMusic(music); else musicManager_->stopMusic();
        playingMusicArea_=musicZone; playingMusicPeriod_=period;
        musicSwitchCooldown_=6.f;
        LOG_INFO("[ZONE_MUSIC] area=",musicAreaId," playlist=",musicZone,
                 " period=",period?"night":"day"," track=",music.empty()?"<silence>":music);
    }

    musicManager_->update(deltaTime);

    // When a track finishes, pick a new random track from the current zone
    if (!musicManager_->isPlaying() && !inTavern_ && !inBlacksmith_ &&
        currentZoneId_ != 0 && musicSwitchCooldown_ <= 0.0f) {
        std::string music = zm->getRandomMusic(musicAreaId, musicTimeHours_);
        if (!music.empty()) {
            playZoneMusic(music);
            musicSwitchCooldown_ = 2.0f;
        }
    }
}

} // namespace audio
} // namespace wowee
