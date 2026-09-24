#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace wowee::game {

inline constexpr size_t kLocalMaxWorldEvents = 32;
inline constexpr size_t kLocalMaxWorldEventActions = 8;
inline constexpr size_t kLocalMaxHolidayDurations = 10;
inline constexpr uint32_t kLocalWorldEventMaxInitialDelayMs = 7u * 24u * 60u * 60u * 1000u;
inline constexpr uint32_t kLocalWorldEventMaxActiveMs = 7u * 24u * 60u * 60u * 1000u;
inline constexpr uint32_t kLocalWorldEventMaxCooldownMs = 30u * 24u * 60u * 60u * 1000u;

enum class LocalWorldEventClock : uint8_t { Simulation = 0, Holiday = 1, Interval = 2 };

/// Holiday and Interval schedules follow the host's local wall clock and are
/// recomputed rather than advanced or restored from a save.
inline bool localWorldEventWallClock(LocalWorldEventClock clock) {
    return clock==LocalWorldEventClock::Holiday || clock==LocalWorldEventClock::Interval;
}
/// Interval bounds: game_event.start_time/end_time as local calendar seconds
/// since 1970-01-01, 1970..2400, and occurence/length in minutes.
inline constexpr int64_t kLocalIntervalMinSeconds = 0;
inline constexpr int64_t kLocalIntervalMaxSeconds = 13569465600LL; // 2400-01-01
inline constexpr uint32_t kLocalIntervalMaxMinutes = 100u * 366u * 24u * 60u;

/// Compact client-owned Holiday.dbc facts used by the local authority. The
/// recurring date rule remains code because Blizzard's 3.3.5a absolute dates
/// are historical; stage lengths stay data-driven from the player's DBC.
struct LocalHolidayDefinition {
    uint32_t id = 0;
    std::array<uint32_t,kLocalMaxHolidayDurations> durationHours{};
    bool operator==(const LocalHolidayDefinition&) const = default;
};

inline bool validLocalHolidayDefinition(const LocalHolidayDefinition& holiday) {
    if(!holiday.id)return false;
    return std::any_of(holiday.durationHours.begin(),holiday.durationHours.end(),[](uint32_t hours){return hours>0&&hours<=24u*60u;});
}

/// Immutable authored schedule. Simulation events advance only while the local
/// authority runs. Holiday events are recomputed from the console's local wall
/// clock and Holiday.dbc, then projected through the same exclusive phase bits.
struct LocalWorldEventSchedule {
    uint32_t id = 0, mapId = 0, instanceId = 0;
    std::string name;
    uint32_t initialDelayMs = 0, activeDurationMs = 0, cooldownMs = 0;
    uint32_t activePhaseMask = 0, inactivePhaseMask = 0;
    std::vector<uint32_t> startActionIds, endActionIds;
    uint32_t holidayId = 0;
    uint8_t holidayStage = 0;
    // Interval clock only (AzerothCore GAMEEVENT_NORMAL schedule).
    int64_t intervalStartSeconds = 0, intervalEndSeconds = 0;
    uint32_t occurrenceMinutes = 0, lengthMinutes = 0;
    LocalWorldEventClock clock = LocalWorldEventClock::Simulation;
    bool enabled = true, repeat = false;
};

/// Durable shared lifecycle. Holiday events intentionally keep remainingMs at
/// zero: wall time is authoritative and is never reconstructed from a save.
struct LocalWorldEventState {
    uint32_t id = 0, revision = 1, remainingMs = 0, cycle = 0;
    bool enabled = false, active = false;
    bool operator==(const LocalWorldEventState&) const = default;
};

enum class LocalWorldEventBoundary : uint8_t { Start = 1, End = 2 };

inline uint32_t localWorldEventNext(uint32_t value) {
    ++value;
    return value ? value : 1;
}

inline bool validLocalWorldEventSchedule(const LocalWorldEventSchedule& event) {
    if (!event.id || event.mapId > 10000 || event.instanceId > 65535 || event.name.empty() || event.name.size() > 96 ||
        event.startActionIds.size() > kLocalMaxWorldEventActions || event.endActionIds.size() > kLocalMaxWorldEventActions ||
        (event.activePhaseMask & event.inactivePhaseMask) || ((event.activePhaseMask | event.inactivePhaseMask) & 1u)) return false;
    const auto validActions=[](const std::vector<uint32_t>& ids) {
        if (std::any_of(ids.begin(),ids.end(),[](uint32_t id){return !id;})) return false;
        auto sorted=ids;std::sort(sorted.begin(),sorted.end());
        return std::adjacent_find(sorted.begin(),sorted.end())==sorted.end();
    };
    if(!validActions(event.startActionIds) || !validActions(event.endActionIds))return false;
    const bool noInterval=!event.intervalStartSeconds && !event.intervalEndSeconds && !event.occurrenceMinutes && !event.lengthMinutes;
    if(event.clock==LocalWorldEventClock::Holiday) {
        return event.holidayId && event.holidayStage>=1 && event.holidayStage<=kLocalMaxHolidayDurations &&
            !event.initialDelayMs && !event.activeDurationMs && !event.cooldownMs && !event.repeat &&
            event.startActionIds.empty() && event.endActionIds.empty() && noInterval;
    }
    if(event.clock==LocalWorldEventClock::Interval) {
        return !event.holidayId && !event.holidayStage && !event.initialDelayMs && !event.activeDurationMs &&
            !event.cooldownMs && !event.repeat && event.startActionIds.empty() && event.endActionIds.empty() &&
            event.intervalStartSeconds>=kLocalIntervalMinSeconds && event.intervalStartSeconds<=kLocalIntervalMaxSeconds &&
            event.intervalEndSeconds>=kLocalIntervalMinSeconds && event.intervalEndSeconds<=kLocalIntervalMaxSeconds &&
            event.occurrenceMinutes>=1 && event.occurrenceMinutes<=kLocalIntervalMaxMinutes &&
            event.lengthMinutes>=1 && event.lengthMinutes<=kLocalIntervalMaxMinutes;
    }
    if(!noInterval)return false;
    if(event.holidayId || event.holidayStage || event.activeDurationMs < 50 || event.activeDurationMs > kLocalWorldEventMaxActiveMs ||
       event.initialDelayMs > kLocalWorldEventMaxInitialDelayMs)return false;
    if (event.repeat) {
        if (event.cooldownMs < 50 || event.cooldownMs > kLocalWorldEventMaxCooldownMs) return false;
    } else if (event.cooldownMs) return false;
    return true;
}

inline LocalWorldEventState localInitialWorldEventState(const LocalWorldEventSchedule& event) {
    if(localWorldEventWallClock(event.clock))return {event.id,1,0,0,event.enabled,false};
    return {event.id,1,event.enabled?event.initialDelayMs:0,0,event.enabled,false};
}

inline bool validLocalWorldEventState(const LocalWorldEventState& state,
                                      const LocalWorldEventSchedule* event=nullptr) {
    if (!state.id || !state.revision || (!state.enabled && (state.active || state.remainingMs)) ||
        (state.active && !state.enabled)) return false;
    if (!event) return true;
    if (!validLocalWorldEventSchedule(*event) || state.id!=event->id) return false;
    if(localWorldEventWallClock(event->clock))
        return !state.remainingMs && (!state.active || state.cycle);
    if (!state.enabled) return true;
    if (state.active) return state.cycle && state.remainingMs<=event->activeDurationMs;
    if (!state.cycle) return state.remainingMs<=event->initialDelayMs;
    return event->repeat && state.remainingMs<=event->cooldownMs;
}

inline bool validLocalWorldEventStates(const std::vector<LocalWorldEventState>& states) {
    if (states.size()>kLocalMaxWorldEvents) return false;
    uint32_t previous=0;
    for (const auto& state:states) {
        if (state.id<=previous || !validLocalWorldEventState(state)) return false;
        previous=state.id;
    }
    return true;
}

inline const LocalWorldEventSchedule* localWorldEventSchedule(
    const std::vector<LocalWorldEventSchedule>& schedules,uint32_t id) {
    const auto it=std::lower_bound(schedules.begin(),schedules.end(),id,
        [](const auto& event,uint32_t value){return event.id<value;});
    return it!=schedules.end()&&it->id==id?&*it:nullptr;
}

inline const LocalHolidayDefinition* localHolidayDefinition(
    const std::vector<LocalHolidayDefinition>& holidays,uint32_t id) {
    const auto it=std::lower_bound(holidays.begin(),holidays.end(),id,
        [](const auto& holiday,uint32_t value){return holiday.id<value;});
    return it!=holidays.end()&&it->id==id?&*it:nullptr;
}

/// Gregorian day number relative to 1970-01-01. This is deliberately timezone
/// free: the authority passes the console's already-local calendar fields.
inline int64_t localCivilDays(int year,unsigned month,unsigned day) {
    year -= month<=2;
    const int era=(year>=0?year:year-399)/400;
    const unsigned yoe=unsigned(year-era*400);
    const unsigned doy=(153*(month+(month>2?-3:9))+2)/5+day-1;
    const unsigned doe=yoe*365+yoe/4-yoe/100+doy;
    return int64_t(era)*146097+int64_t(doe)-719468;
}
inline unsigned localWeekday(int year,unsigned month,unsigned day) {
    int64_t value=(localCivilDays(year,month,day)+4)%7;if(value<0)value+=7;
    return unsigned(value); // Sunday=0
}
inline int64_t localDateSeconds(int year,unsigned month,unsigned day,unsigned hour=0,unsigned minute=0,unsigned second=0) {
    return localCivilDays(year,month,day)*86400+int64_t(hour)*3600+int64_t(minute)*60+second;
}
struct LocalCalendarTime {
    int year=0;unsigned month=0,day=0,hour=0,minute=0,second=0;
};
inline bool validLocalCalendarTime(const LocalCalendarTime& time) {
    return time.year>=1970&&time.year<=2400&&time.month>=1&&time.month<=12&&time.day>=1&&time.day<=31&&
        time.hour<24&&time.minute<60&&time.second<60;
}

inline void localHolidayBaseStarts(uint32_t holidayId,int year,std::vector<int64_t>& starts) {
    const auto fixed=[&](unsigned month,unsigned day){starts.push_back(localDateSeconds(year,month,day));};
    switch(holidayId) {
        case 341: fixed(6,21); break; // Midsummer Fire Festival
        case 141: fixed(12,15); break; // Feast of Winter Veil
        case 324: fixed(10,18); break; // Hallow's End
        case 409: fixed(11,1); break; // Day of the Dead
        case 372: fixed(9,13); break; // Brewfest preparation; stage offset selects the festival
        case 404: { // Pilgrim's Bounty: Sunday before the fourth Thursday in November.
            const unsigned firstThursday=1+(4+7-localWeekday(year,11,1))%7;
            starts.push_back(localDateSeconds(year,11,firstThursday+21)-4*86400LL);
            break;
        }
        case 374: { // Darkmoon Faire: build begins two days before each first Sunday.
            for(unsigned month: {3u,6u,9u,12u}) {
                const unsigned firstSunday=1+(7-localWeekday(year,month,1))%7;
                starts.push_back(localDateSeconds(year,month,firstSunday)-2*86400LL);
            }
            break;
        }
        default: break;
    }
}

/// Resolve a holiday event against one local wall-clock instant. `resolved`
/// distinguishes a legitimate inactive event from missing/unsupported DBC
/// data; callers fail closed when it is false.
inline bool localHolidayEventActive(const LocalWorldEventSchedule& event,
                                    const std::vector<LocalHolidayDefinition>& holidays,
                                    const LocalCalendarTime& now,bool& resolved) {
    resolved=false;
    if(event.clock!=LocalWorldEventClock::Holiday || !validLocalWorldEventSchedule(event) || !validLocalCalendarTime(now))return false;
    const auto* holiday=localHolidayDefinition(holidays,event.holidayId);if(!holiday)return false;
    const size_t stage=size_t(event.holidayStage-1);const uint32_t duration=holiday->durationHours[stage];if(!duration)return false;
    uint64_t offsetHours=0;for(size_t i=0;i<stage;++i)offsetHours+=holiday->durationHours[i];
    if(offsetHours>24u*365u || duration>24u*60u)return false;
    const int64_t current=localDateSeconds(now.year,now.month,now.day,now.hour,now.minute,now.second);
    bool hasRule=false;
    for(int year=now.year-1;year<=now.year+1;++year) {
        std::vector<int64_t> starts;localHolidayBaseStarts(event.holidayId,year,starts);hasRule=hasRule||!starts.empty();
        for(const auto base:starts) {
            const int64_t begin=base+int64_t(offsetHours)*3600;
            const int64_t end=begin+int64_t(duration)*3600;
            if(current>=begin&&current<end){resolved=true;return true;}
        }
    }
    resolved=hasRule;return false;
}

/// AzerothCore GameEventMgr::CheckOneGameEvent for GAMEEVENT_NORMAL:
/// start < now < end and (now-start) % occurence < length, all in the host's
/// local calendar seconds. An end at or before the start is never active.
inline bool localIntervalEventActive(const LocalWorldEventSchedule& event,const LocalCalendarTime& now,bool& resolved) {
    resolved=false;
    if(event.clock!=LocalWorldEventClock::Interval || !validLocalWorldEventSchedule(event) || !validLocalCalendarTime(now))return false;
    resolved=true;
    const int64_t current=localDateSeconds(now.year,now.month,now.day,now.hour,now.minute,now.second);
    if(!(event.intervalStartSeconds<current && current<event.intervalEndSeconds))return false;
    const int64_t period=int64_t(event.occurrenceMinutes)*60,length=int64_t(event.lengthMinutes)*60;
    return (current-event.intervalStartSeconds)%period<length;
}

/// Advance one simulation schedule, with an all-or-nothing boundary callback.
/// Holiday schedules are recomputed by the authority and never enter here.
template<class BoundaryCommit>
bool localAdvanceWorldEvent(LocalWorldEventState& state,const LocalWorldEventSchedule& event,
                            uint32_t elapsedMs,BoundaryCommit&& commit,bool& changed) {
    changed=false;
    if (event.clock!=LocalWorldEventClock::Simulation || !validLocalWorldEventState(state,&event) || !state.enabled) return false;
    uint32_t transitions=0;
    while (state.enabled) {
        if (state.remainingMs>elapsedMs) {
            if(elapsedMs){state.remainingMs-=elapsedMs;changed=true;}
            return true;
        }
        elapsedMs-=state.remainingMs;
        LocalWorldEventState next=state;
        const auto boundary=state.active?LocalWorldEventBoundary::End:LocalWorldEventBoundary::Start;
        if(boundary==LocalWorldEventBoundary::Start) {
            next.active=true;next.remainingMs=event.activeDurationMs;
            next.cycle=localWorldEventNext(next.cycle);
        } else if(event.repeat) {
            next.active=false;next.remainingMs=event.cooldownMs;
        } else {
            next.active=false;next.enabled=false;next.remainingMs=0;
        }
        next.revision=localWorldEventNext(next.revision);
        if(!commit(boundary,event,next)){state.remainingMs=0;changed=true;return true;}
        state=next;changed=true;
        if(++transitions>=8 || !elapsedMs) return true;
    }
    return true;
}

} // namespace wowee::game
