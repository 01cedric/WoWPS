cmake_minimum_required(VERSION 3.20)
project(wowee VERSION 1.0.0 LANGUAGES C CXX)

if(NOT WOWEE_PS4 OR NOT OO_PS4_TOOLCHAIN)
    message(FATAL_ERROR "Use tools/ps4/build_ps4_pkg.sh or configure with -DCMAKE_TOOLCHAIN_FILE=cmake/ps4.toolchain.cmake. See README.md.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Explicitly tag optimized configs so runtime defaults can enforce low-noise logging.
add_compile_definitions(
    $<$<CONFIG:Release>:WOWEE_RELEASE_LOGGING>
    $<$<CONFIG:RelWithDebInfo>:WOWEE_RELEASE_LOGGING>
    $<$<CONFIG:MinSizeRel>:WOWEE_RELEASE_LOGGING>
)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# OpenOrbis toolchain and platform configuration.
add_compile_definitions(WOWEE_PS4=1)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(WOWEE_PS4_TOOLS_DIR "${OO_PS4_TOOLCHAIN}/bin/macos")
else()
    set(WOWEE_PS4_TOOLS_DIR "${OO_PS4_TOOLCHAIN}/bin/linux")
endif()
set(BUILD_SHARED_LIBS OFF)
set(WOWEE_ENABLE_ASAN OFF)
set(WOWEE_ENABLE_TRACY OFF)
set(WOWEE_ENABLE_AMD_FSR2 OFF)
set(WOWEE_ENABLE_AMD_FSR3_FRAMEGEN OFF)
set(WOWEE_BUILD_AMD_FSR3_RUNTIME OFF)
add_compile_definitions(WOWEE_HAS_AMD_FSR2=0 WOWEE_HAS_AMD_FSR3_FRAMEGEN=0 WOWEE_AMD_FFX_SDK_KITS=0)

# Protocol tables and generated include fragments.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
if(Python3_Interpreter_FOUND)
    add_custom_target(opcodes-generate
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/gen_opcode_registry.py
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating opcode registry include fragments"
    )

    add_custom_target(opcodes-validate
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/validate_opcode_maps.py --root ${CMAKE_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS opcodes-generate
        COMMENT "Validating canonical opcode registry and expansion maps"
    )
endif()

# Bundled PS4 dependencies.
include(cmake/ps4/stormlib.cmake)
include(cmake/ps4/crypto_shim.cmake)
include(cmake/ps4/ps4_vulkan.cmake)
set(Vulkan_FOUND TRUE)
    find_package(Threads QUIET)
    if(NOT TARGET Threads::Threads)
        add_library(wowee_ps4_threads INTERFACE)
        add_library(Threads::Threads ALIAS wowee_ps4_threads)
    endif()
find_package(ZLIB REQUIRED)
set(HAVE_FFMPEG FALSE)
set(HAVE_UNICORN FALSE)
    set(WOWEE_PS4_GLM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/glm)
    if(NOT EXISTS ${WOWEE_PS4_GLM_DIR}/glm/glm.hpp)
        message(FATAL_ERROR "PS4 build needs glm vendored at extern/glm: "
                            "git clone --depth 1 --branch 1.0.1 https://github.com/g-truc/glm ${WOWEE_PS4_GLM_DIR}")
    endif()
    add_library(wowee_ps4_glm INTERFACE)
    target_include_directories(wowee_ps4_glm SYSTEM INTERFACE ${WOWEE_PS4_GLM_DIR})
    add_library(glm::glm-header-only ALIAS wowee_ps4_glm)
    set(glm_FOUND TRUE)
    set(GLM_INCLUDE_DIRS ${WOWEE_PS4_GLM_DIR})
    message(STATUS "PS4: glm vendored at ${WOWEE_PS4_GLM_DIR}")
add_compile_definitions(GLM_ENABLE_EXPERIMENTAL GLM_FORCE_DEPTH_ZERO_TO_ONE)
set(GLSLC GLSLC-NOTFOUND)

# User interface dependencies.
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui)
if(EXISTS ${IMGUI_DIR})
    set(IMGUI_SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
    )
    if(WOWEE_PS4)
        set(WOWEE_PS4_IMGUI_BACKEND ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/ps4/imgui_impl_ps4.cpp)
        if(EXISTS ${WOWEE_PS4_IMGUI_BACKEND})
            list(APPEND IMGUI_SOURCES ${WOWEE_PS4_IMGUI_BACKEND})
        else()
            message(STATUS "PS4: ${WOWEE_PS4_IMGUI_BACKEND} not present yet; imgui built without a platform backend")
        endif()
    else()
    endif()
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)
    add_library(imgui STATIC ${IMGUI_SOURCES})
    target_include_directories(imgui SYSTEM PUBLIC
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
    )
    if(WOWEE_PS4)
        target_include_directories(imgui BEFORE PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/platform/ps4
            ${CMAKE_CURRENT_SOURCE_DIR}/ps4/compat/cxx20
            ${CMAKE_CURRENT_SOURCE_DIR}/ps4/compat)
        target_include_directories(imgui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_link_libraries(imgui PUBLIC Vulkan::Vulkan)
    else()
        target_link_libraries(imgui PUBLIC SDL2::SDL2 Vulkan::Vulkan ${CMAKE_DL_LIBS})
    endif()
else()
    message(WARNING "ImGui not found in extern/imgui. Clone it with:")
    message(WARNING "  git clone https://github.com/ocornut/imgui.git extern/imgui")
endif()
set(VK_BOOTSTRAP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/vk-bootstrap)
if(EXISTS ${VK_BOOTSTRAP_DIR})
    add_library(vk-bootstrap STATIC
        ${VK_BOOTSTRAP_DIR}/src/VkBootstrap.cpp
    )
    target_include_directories(vk-bootstrap SYSTEM PUBLIC ${VK_BOOTSTRAP_DIR}/src)
    target_link_libraries(vk-bootstrap PUBLIC Vulkan::Vulkan)
else()
    message(FATAL_ERROR "vk-bootstrap not found in extern/vk-bootstrap")
endif()

# Client sources and public headers.
set(WOWEE_SOURCES
    # Core
    src/core/application.cpp
    src/core/application_local_realm.cpp
    src/core/application_local_ui.cpp
    src/core/application_local_services_ui.cpp
    src/core/application_character_intro.cpp
    src/core/character_intro.cpp
    src/pipeline/character_intro.cpp
    src/core/entity_spawner.cpp
    src/core/entity_spawner_player.cpp
    src/core/entity_spawner_processing.cpp
    src/core/appearance_composer.cpp
    src/core/helm_visual.cpp
    src/core/world_loader.cpp
    src/core/npc_interaction_callback_handler.cpp
    src/core/audio_callback_handler.cpp
    src/core/entity_spawn_callback_handler.cpp
    src/core/animation_callback_handler.cpp
    src/core/transport_callback_handler.cpp
    src/core/world_entry_callback_handler.cpp
    src/core/ui_screen_callback_handler.cpp
    src/core/window.cpp
    src/core/input.cpp
    src/core/logger.cpp
    src/core/memory_monitor.cpp
    src/core/app_clock.cpp
    src/core/config_paths.cpp

    # Math
    src/math/spline.cpp

    # Network
    src/network/socket.cpp
    src/network/packet.cpp
    src/network/tcp_socket.cpp
    src/network/world_socket.cpp

    # Auth
    src/auth/auth_handler.cpp
    src/auth/auth_opcodes.cpp
    src/auth/auth_packets.cpp
    src/auth/pin_auth.cpp
    src/auth/integrity.cpp
    src/auth/srp.cpp
    src/auth/big_num.cpp
    src/auth/crypto.cpp
    src/auth/rc4.cpp
    src/auth/vanilla_crypt.cpp

    # Game
    src/game/expansion_profile.cpp
    src/game/opcode_table.cpp
    src/game/update_field_table.cpp
    src/game/game_handler.cpp
    src/game/game_handler_local.cpp
    src/game/game_handler_local_auction.cpp
    src/game/local_realm.cpp
    src/game/local_gameplay.cpp
    src/game/local_services.cpp
    src/game/local_travel.cpp
    src/game/local_bots.cpp
    src/game/local_world_catalog.cpp
    src/game/game_handler_packets.cpp
    src/game/game_handler_callbacks.cpp
    src/game/chat_handler.cpp
    src/game/text_tokens.cpp
    src/game/text_tokens_subject.cpp
    src/game/movement_handler.cpp
    src/game/combat_handler.cpp
    src/game/spell_handler.cpp
    src/game/inventory_handler.cpp
    src/game/social_handler.cpp
    src/game/quest_handler.cpp
    src/game/entity_controller.cpp
    src/game/warden_handler.cpp
    src/game/warden_crypto.cpp
    src/game/cata_movement.cpp
    src/game/warden_module.cpp
    src/game/warden_emulator.cpp
    src/game/warden_memory.cpp
    src/game/transport_manager.cpp
    src/game/transport_path_repository.cpp
    src/game/transport_clock_sync.cpp
    src/game/transport_animator.cpp
    src/game/world.cpp
    src/game/player.cpp
    src/game/entity.cpp
    src/game/opcodes.cpp
    src/game/world_packets.cpp
    src/game/world_packets_social.cpp
    src/game/world_packets_entity.cpp
    src/game/world_packets_world.cpp
    src/game/world_packets_economy.cpp
    src/game/spline_packet.cpp
    src/game/shapeshift_forms.cpp
    src/game/calendar_data.cpp
    src/game/faction_hostility.cpp
    src/game/packet_parsers_tbc.cpp
    src/game/packet_parsers_classic.cpp
    src/game/character.cpp
    src/game/zone_manager.cpp
    src/game/inventory.cpp

    # Audio
    src/audio/audio_engine.cpp
    src/audio/audio_coordinator.cpp
    src/audio/music_manager.cpp
    src/audio/footstep_manager.cpp
    src/audio/activity_sound_manager.cpp
    src/audio/mount_sound_manager.cpp
    src/audio/npc_voice_manager.cpp
    src/audio/player_voice_manager.cpp
    src/audio/ambient_sound_manager.cpp
    src/audio/ui_sound_manager.cpp
    src/audio/combat_sound_manager.cpp
    src/audio/spell_sound_manager.cpp
    src/audio/movement_sound_manager.cpp

    # Pipeline (asset loaders)
    src/pipeline/blp_loader.cpp
    src/pipeline/dbc_loader.cpp
    src/pipeline/asset_manager.cpp
    src/pipeline/asset_manifest.cpp
    src/pipeline/loose_file_reader.cpp
    src/pipeline/mpq_asset_source.cpp
    src/pipeline/m2_loader.cpp
    src/pipeline/wmo_loader.cpp
    src/pipeline/adt_loader.cpp
    src/pipeline/wdt_loader.cpp
    src/pipeline/wowee_terrain_loader.cpp
    src/pipeline/wowee_model.cpp
    src/pipeline/wowee_model_fromm2.cpp
    src/pipeline/wowee_building.cpp
    src/pipeline/wowee_collision.cpp
    src/pipeline/wowee_light.cpp
    src/pipeline/wowee_weather.cpp
    src/pipeline/wowee_world_map.cpp
    src/pipeline/wowee_sound.cpp
    src/pipeline/wowee_spawns.cpp
    src/pipeline/wowee_items.cpp
    src/pipeline/wowee_loot.cpp
    src/pipeline/wowee_creatures.cpp
    src/pipeline/wowee_quests.cpp
    src/pipeline/wowee_objects.cpp
    src/pipeline/wowee_factions.cpp
    src/pipeline/wowee_locks.cpp
    src/pipeline/wowee_skills.cpp
    src/pipeline/wowee_spells.cpp
    src/pipeline/wowee_achievements.cpp
    src/pipeline/wowee_trainers.cpp
    src/pipeline/wowee_gossip.cpp
    src/pipeline/wowee_taxi.cpp
    src/pipeline/wowee_talents.cpp
    src/pipeline/wowee_maps.cpp
    src/pipeline/wowee_chars.cpp
    src/pipeline/wowee_tokens.cpp
    src/pipeline/wowee_triggers.cpp
    src/pipeline/wowee_titles.cpp
    src/pipeline/wowee_events.cpp
    src/pipeline/wowee_mounts.cpp
    src/pipeline/wowee_battlegrounds.cpp
    src/pipeline/wowee_mail.cpp
    src/pipeline/wowee_gems.cpp
    src/pipeline/wowee_guilds.cpp
    src/pipeline/wowee_conditions.cpp
    src/pipeline/wowee_pets.cpp
    src/pipeline/wowee_auction.cpp
    src/pipeline/wowee_channels.cpp
    src/pipeline/wowee_cinematics.cpp
    src/pipeline/wowee_glyphs.cpp
    src/pipeline/wowee_vehicles.cpp
    src/pipeline/wowee_holidays.cpp
    src/pipeline/wowee_liquids.cpp
    src/pipeline/wowee_animations.cpp
    src/pipeline/wowee_spell_visuals.cpp
    src/pipeline/wowee_world_state_ui.cpp
    src/pipeline/wowee_player_conditions.cpp
    src/pipeline/wowee_trade_skills.cpp
    src/pipeline/wowee_creature_equipment.cpp
    src/pipeline/wowee_item_sets.cpp
    src/pipeline/wowee_game_tips.cpp
    src/pipeline/wowee_companions.cpp
    src/pipeline/wowee_spell_mechanics.cpp
    src/pipeline/wowee_keybindings.cpp
    src/pipeline/wowee_spell_schools.cpp
    src/pipeline/wowee_lfg.cpp
    src/pipeline/wowee_macros.cpp
    src/pipeline/wowee_char_features.cpp
    src/pipeline/wowee_pvp.cpp
    src/pipeline/wowee_bags.cpp
    src/pipeline/wowee_runes.cpp
    src/pipeline/wowee_loading_screens.cpp
    src/pipeline/wowee_item_suffixes.cpp
    src/pipeline/wowee_combat_ratings.cpp
    src/pipeline/wowee_unit_movement.cpp
    src/pipeline/wowee_quest_sorts.cpp
    src/pipeline/wowee_spell_ranges.cpp
    src/pipeline/wowee_spell_cast_times.cpp
    src/pipeline/wowee_spell_durations.cpp
    src/pipeline/wowee_spell_cooldowns.cpp
    src/pipeline/wowee_creature_families.cpp
    src/pipeline/wowee_spell_power_costs.cpp
    src/pipeline/wowee_glyph_slots.cpp
    src/pipeline/wowee_creature_difficulties.cpp
    src/pipeline/wowee_item_materials.cpp
    src/pipeline/wowee_player_spawn_profiles.cpp
    src/pipeline/wowee_talent_tabs.cpp
    src/pipeline/wowee_currency_types.cpp
    src/pipeline/wowee_spell_reagents.cpp
    src/pipeline/wowee_achievement_criteria.cpp
    src/pipeline/wowee_spell_effect_types.cpp
    src/pipeline/wowee_spell_aura_types.cpp
    src/pipeline/wowee_item_qualities.cpp
    src/pipeline/wowee_skill_costs.cpp
    src/pipeline/wowee_item_flags.cpp
    src/pipeline/wowee_npc_services.cpp
    src/pipeline/wowee_token_rewards.cpp
    src/pipeline/wowee_spell_procs.cpp
    src/pipeline/wowee_creature_patrols.cpp
    src/pipeline/wowee_boss_encounters.cpp
    src/pipeline/wowee_instance_lockouts.cpp
    src/pipeline/wowee_stable_slots.cpp
    src/pipeline/wowee_stat_curves.cpp
    src/pipeline/wowee_action_bars.cpp
    src/pipeline/wowee_group_compositions.cpp
    src/pipeline/wowee_hearth_binds.cpp
    src/pipeline/wowee_server_broadcasts.cpp
    src/pipeline/wowee_combat_maneuvers.cpp
    src/pipeline/wowee_realm_list.cpp
    src/pipeline/wowee_emotes.cpp
    src/pipeline/wowee_buff_book.cpp
    src/pipeline/wowee_tabards.cpp
    src/pipeline/wowee_spell_markers.cpp
    src/pipeline/wowee_learning_notifications.cpp
    src/pipeline/wowee_creature_resists.cpp
    src/pipeline/wowee_pet_talents.cpp
    src/pipeline/wowee_heroic_scaling.cpp
    src/pipeline/wowee_reputation_rewards.cpp
    src/pipeline/wowee_minimap_levels.cpp
    src/pipeline/wowee_pet_care.cpp
    src/pipeline/wowee_movie_credits.cpp
    src/pipeline/wowee_spell_variants.cpp
    src/pipeline/wowee_voiceovers.cpp
    src/pipeline/wowee_trade_rules.cpp
    src/pipeline/wowee_word_filters.cpp
    src/pipeline/wowee_raid_markers.cpp
    src/pipeline/wowee_loot_modes.cpp
    src/pipeline/wowee_sky_params.cpp
    src/pipeline/wowee_server_config.cpp
    src/pipeline/wowee_anniversary_events.cpp
    src/pipeline/wowee_pvp_ranks.cpp
    src/pipeline/wowee_localization.cpp
    src/pipeline/wowee_global_channels.cpp
    src/pipeline/wowee_addon_manifest.cpp
    src/pipeline/wowee_spell_pack.cpp
    src/pipeline/wowee_player_movement_anim.cpp
    src/pipeline/wowee_transit_schedule.cpp
    src/pipeline/wowee_mage_portals.cpp
    src/pipeline/wowee_combat_stats.cpp
    src/pipeline/wowee_guild_bank.cpp
    src/pipeline/wowee_quest_graph.cpp
    src/pipeline/wowee_crafting_recipes.cpp
    src/pipeline/wowee_world_locations.cpp
    src/pipeline/wowee_soulbind_rules.cpp
    src/pipeline/wowee_creature_behavior.cpp
    src/pipeline/wowee_random_property.cpp
    src/pipeline/wowee_spell_proc_rules.cpp
    src/pipeline/wowee_auction_houses.cpp
    src/pipeline/wowee_battleground_rewards.cpp
    src/pipeline/wowee_sound_swap.cpp
    src/pipeline/wowee_tutorial_steps.cpp
    src/pipeline/wowee_chat_commands.cpp
    src/pipeline/wowee_camera_presets.cpp
    src/pipeline/wowee_combat_formulas.cpp
    src/pipeline/wowee_chat_links.cpp
    src/pipeline/custom_zone_discovery.cpp
    src/pipeline/char_sections.cpp
    src/ui/settings_schema.cpp
    src/pipeline/item_textures.cpp
    src/pipeline/m2_asset_loader.cpp
    src/pipeline/dbc_layout.cpp
    src/pipeline/spell_icon_paths.cpp

    src/pipeline/terrain_mesh.cpp

    # Rendering (Vulkan infrastructure)
    src/rendering/vk_context.cpp
    src/rendering/vk_utils.cpp
    src/rendering/vk_shader.cpp
    src/rendering/vk_texture.cpp
    src/rendering/vk_buffer.cpp
    src/rendering/vk_pipeline.cpp
    src/rendering/vk_render_target.cpp

    # Rendering
    src/rendering/renderer.cpp
    src/rendering/amd_fsr3_runtime.cpp
    src/rendering/camera.cpp
    src/rendering/camera_controller.cpp
    src/rendering/material.cpp
    src/rendering/terrain_renderer.cpp
    src/rendering/shadow_params.cpp
    src/rendering/terrain_manager.cpp
    src/rendering/frustum.cpp
    src/rendering/performance_hud.cpp
    src/rendering/water_renderer.cpp
    src/rendering/skybox.cpp
    src/rendering/celestial.cpp
    src/rendering/starfield.cpp
    src/rendering/clouds.cpp
    src/rendering/lens_flare.cpp
    src/rendering/weather.cpp
    src/rendering/lightning.cpp
    src/rendering/lighting_manager.cpp
    src/rendering/zone_ambience.cpp
    src/rendering/sky_system.cpp
    src/rendering/character_renderer.cpp
    src/rendering/character_preview.cpp
    src/rendering/glue_scene_effects.cpp
    src/rendering/wmo_renderer.cpp
    src/rendering/imgui_texture.cpp
    src/rendering/bone_slots.cpp
    src/rendering/normal_map.cpp
    src/rendering/m2_renderer.cpp
    src/rendering/m2_renderer_render.cpp
    src/rendering/m2_renderer_particles.cpp
    src/rendering/m2_renderer_instance.cpp
    src/rendering/m2_model_classifier.cpp
    src/rendering/render_graph.cpp
    src/rendering/hiz_system.cpp
    src/rendering/grass_renderer.cpp
    src/pipeline/adt_alpha.cpp
    src/pipeline/grass_terrain.cpp
    src/pipeline/grass_population.cpp
    src/pipeline/grass_profile.cpp
    src/pipeline/grass_biomes.cpp
    src/pipeline/grass_clearing.cpp
    src/rendering/quest_marker_renderer.cpp
    src/rendering/footprint_renderer.cpp
    src/rendering/minimap.cpp
    src/rendering/world_map/coordinate_projection.cpp
    src/rendering/world_map/map_resolver.cpp
    src/rendering/world_map/exploration_state.cpp
    src/rendering/world_map/zone_metadata.cpp
    src/rendering/world_map/data_repository.cpp
    src/rendering/world_map/view_state_machine.cpp
    src/rendering/world_map/composite_renderer.cpp
    src/rendering/world_map/overlay_renderer.cpp
    src/rendering/world_map/input_handler.cpp
    src/rendering/world_map/world_map_facade.cpp
    src/rendering/world_map/layers/player_marker_layer.cpp
    src/rendering/world_map/layers/party_dot_layer.cpp
    src/rendering/world_map/layers/taxi_node_layer.cpp
    src/rendering/world_map/layers/poi_marker_layer.cpp
    src/rendering/world_map/layers/quest_poi_layer.cpp
    src/rendering/world_map/layers/corpse_marker_layer.cpp
    src/rendering/world_map/layers/rare_tracker_layer.cpp
    src/rendering/world_map/layers/chest_tracker_layer.cpp
    src/rendering/world_map/layers/zone_highlight_layer.cpp
    src/rendering/world_map/layers/coordinate_display.cpp
    src/rendering/world_map/layers/subzone_tooltip_layer.cpp
    src/rendering/swim_effects.cpp
    src/rendering/mount_dust.cpp
    src/rendering/levelup_effect.cpp
    src/rendering/charge_effect.cpp
    src/rendering/spell_visual_system.cpp
    src/rendering/post_process_pipeline.cpp
    src/rendering/overlay_system.cpp
    src/rendering/animation_controller.cpp
    src/rendering/animation/animation_ids.cpp
    src/rendering/animation/emote_registry.cpp
    src/rendering/animation/footstep_driver.cpp
    src/rendering/animation/sfx_state_driver.cpp
    src/rendering/animation/anim_capability_probe.cpp
    src/rendering/animation/locomotion_fsm.cpp
    src/rendering/animation/combat_fsm.cpp
    src/rendering/animation/activity_fsm.cpp
    src/rendering/animation/mount_fsm.cpp
    src/rendering/animation/character_animator.cpp  # Renamed from player_animator.cpp; npc_animator.cpp removed
    src/rendering/animation/animation_manager.cpp
    src/rendering/loading_screen.cpp

    # UI
    src/ui/ui_manager.cpp
    src/ui/ui_upload_budget.cpp
    src/ui/widget_tree.cpp
    src/ui/xml_parser.cpp
    src/ui/framexml_emitter.cpp
    src/ui/framexml_takeover.cpp
    src/ui/unit_portrait.cpp
    src/ui/interface_fonts.cpp
    src/ui/text_edit.cpp
    src/ui/paper_ui.cpp
    src/ui/wotlk_glue.cpp
    src/ui/widget_renderer.cpp
    src/ui/ui_raid_icons.cpp
    src/ui/ui_texture_load.cpp
    src/ui/auth_screen.cpp
    src/ui/realm_screen.cpp
    src/ui/character_create_screen.cpp
    src/ui/character_screen.cpp
    src/ui/escape_action.cpp
    src/ui/game_screen.cpp
    src/ui/scene_pick.cpp
    src/ui/game_screen_frames.cpp
    src/ui/game_screen_hud.cpp
    src/ui/game_screen_minimap.cpp
    src/ui/chat_panel.cpp
    src/ui/macro_text.cpp
    src/ui/chat/chat_settings.cpp
    src/ui/chat/chat_utils.cpp
    src/ui/chat/chat_bubble_manager.cpp
    src/ui/chat/chat_markup_parser.cpp
    src/ui/plural_escape.cpp
    src/ui/chat/chat_markup_renderer.cpp
    src/ui/chat/item_tooltip_renderer.cpp
    src/ui/chat/chat_command_registry.cpp
    src/ui/chat/macro_evaluator.cpp
    src/ui/chat/macro_eval_convenience.cpp
    src/ui/chat/game_state_adapter.cpp
    src/ui/chat/input_modifier_adapter.cpp
    src/ui/chat/commands/system_commands.cpp
    src/ui/chat/commands/social_commands.cpp
    src/ui/chat/commands/channel_commands.cpp
    src/ui/chat/commands/combat_commands.cpp
    src/ui/chat/commands/group_commands.cpp
    src/ui/chat/commands/guild_commands.cpp
    src/ui/chat/commands/target_commands.cpp
    src/ui/chat/commands/emote_commands.cpp
    src/ui/chat/commands/misc_commands.cpp
    src/ui/chat/commands/help_commands.cpp
    src/ui/chat/commands/gm_commands.cpp
    src/ui/toast_manager.cpp
    src/ui/dialog_manager.cpp
    src/ui/settings_panel.cpp
    src/ui/touch_controls.cpp
    src/ui/combat_ui.cpp
    src/ui/window_manager.cpp
    src/ui/bis_gear_data.cpp
    src/ui/inventory_screen.cpp
    src/ui/item_tooltip.cpp
    src/ui/spellbook_screen.cpp
    src/ui/keybinding_manager.cpp

    # Addons
    src/addons/local_framexml.cpp
    src/addons/local_framexml_input.cpp
    src/addons/addon_manager.cpp
    src/addons/lua_engine.cpp
    src/addons/lua_handler_globals.cpp
    src/addons/lua_unit_api.cpp
    src/addons/lua_spell_api.cpp
    src/addons/lua_inventory_api.cpp
    src/addons/lua_quest_api.cpp
    src/addons/lua_lfg_api.cpp
    src/addons/lua_socket_api.cpp
    src/addons/lua_social_api.cpp
    src/addons/lua_system_api.cpp
    src/addons/lua_action_api.cpp
    src/addons/toc_parser.cpp

    # Main
    src/main.cpp
)

set(WOWEE_HEADERS
    include/core/application.hpp
    include/core/window.hpp
    include/core/input.hpp
    include/core/logger.hpp

    include/network/socket.hpp
    include/network/packet.hpp
    include/network/tcp_socket.hpp
    include/network/world_socket.hpp
    include/network/net_platform.hpp

    include/platform/process.hpp

    include/auth/auth_handler.hpp
    include/auth/auth_opcodes.hpp
    include/auth/auth_packets.hpp
    include/auth/srp.hpp
    include/auth/big_num.hpp
    include/auth/crypto.hpp

    include/game/game_handler.hpp
    include/game/world.hpp
    include/game/player.hpp
    include/game/entity.hpp
    include/game/opcodes.hpp
    include/game/zone_manager.hpp
    include/game/inventory.hpp
    include/game/spell_defines.hpp
    include/game/group_defines.hpp
    include/game/world_packets.hpp
    include/game/character.hpp

    include/audio/audio_engine.hpp
    include/audio/music_manager.hpp
    include/audio/footstep_manager.hpp
    include/audio/activity_sound_manager.hpp
    include/audio/mount_sound_manager.hpp
    include/audio/npc_voice_manager.hpp
    include/audio/player_voice_manager.hpp
    include/audio/ambient_sound_manager.hpp
    include/audio/ui_sound_manager.hpp
    include/audio/combat_sound_manager.hpp
    include/audio/spell_sound_manager.hpp
    include/audio/movement_sound_manager.hpp

    include/pipeline/blp_loader.hpp
    include/pipeline/asset_manifest.hpp
    include/pipeline/loose_file_reader.hpp
    include/pipeline/m2_loader.hpp
    include/pipeline/wmo_loader.hpp
    include/pipeline/adt_loader.hpp
    include/pipeline/wdt_loader.hpp
    include/pipeline/dbc_loader.hpp
    include/pipeline/terrain_mesh.hpp

    include/rendering/vk_context.hpp
    include/rendering/vk_utils.hpp
    include/rendering/vk_shader.hpp
    include/rendering/vk_texture.hpp
    include/rendering/vk_buffer.hpp
    include/rendering/vk_pipeline.hpp
    include/rendering/vk_render_target.hpp
    include/rendering/renderer.hpp
    include/rendering/camera.hpp
    include/rendering/camera_controller.hpp
    include/rendering/material.hpp
    include/rendering/terrain_renderer.hpp
    include/rendering/terrain_manager.hpp
    include/rendering/frustum.hpp
    include/rendering/performance_hud.hpp
    include/rendering/water_renderer.hpp
    include/rendering/skybox.hpp
    include/rendering/celestial.hpp
    include/rendering/starfield.hpp
    include/rendering/clouds.hpp
    include/rendering/lens_flare.hpp
    include/rendering/weather.hpp
    include/rendering/lightning.hpp
    include/rendering/swim_effects.hpp
    include/rendering/world_map.hpp
    include/rendering/world_map/world_map_types.hpp
    include/rendering/world_map/coordinate_projection.hpp
    include/rendering/world_map/map_resolver.hpp
    include/rendering/world_map/exploration_state.hpp
    include/rendering/world_map/zone_metadata.hpp
    include/rendering/world_map/data_repository.hpp
    include/rendering/world_map/view_state_machine.hpp
    include/rendering/world_map/composite_renderer.hpp
    include/rendering/world_map/overlay_renderer.hpp
    include/rendering/world_map/input_handler.hpp
    include/rendering/world_map/world_map_facade.hpp
    include/rendering/world_map/layers/player_marker_layer.hpp
    include/rendering/world_map/layers/party_dot_layer.hpp
    include/rendering/world_map/layers/taxi_node_layer.hpp
    include/rendering/world_map/layers/poi_marker_layer.hpp
    include/rendering/world_map/layers/quest_poi_layer.hpp
    include/rendering/world_map/layers/corpse_marker_layer.hpp
    include/rendering/world_map/layers/rare_tracker_layer.hpp
    include/rendering/world_map/layers/chest_tracker_layer.hpp
    include/rendering/world_map/layers/zone_highlight_layer.hpp
    include/rendering/world_map/layers/coordinate_display.hpp
    include/rendering/world_map/layers/subzone_tooltip_layer.hpp
    include/rendering/character_renderer.hpp
    include/rendering/character_preview.hpp
    include/rendering/wmo_renderer.hpp
    include/rendering/loading_screen.hpp

    include/ui/ui_manager.hpp
    include/ui/auth_screen.hpp
    include/ui/realm_screen.hpp
    include/ui/character_create_screen.hpp
    include/ui/character_screen.hpp
    include/ui/game_screen.hpp
    include/ui/inventory_screen.hpp
    include/ui/spellbook_screen.hpp
    include/ui/keybinding_manager.hpp
)

# Console platform and in-process MPQ extraction.
set(WOWEE_PLATFORM_SOURCES)
if(WOWEE_PS4)
    file(GLOB WOWEE_PS4_PLATFORM_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/ps4/*.cpp)
    list(FILTER WOWEE_PS4_PLATFORM_SOURCES EXCLUDE REGEX "imgui_impl_ps4\\.cpp$")
    list(APPEND WOWEE_PLATFORM_SOURCES ${WOWEE_PS4_PLATFORM_SOURCES})
    list(APPEND WOWEE_PLATFORM_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/asset_extract/extractor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/asset_extract/path_mapper.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/asset_extract/manifest_writer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/asset_extract/open_format_emitter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/asset_extract/first_launch.cpp)
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/mpq_asset_source.cpp
        PROPERTIES COMPILE_DEFINITIONS WOWEE_MPQ_SOURCE_AVAILABLE=1)
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/asset_extract/open_format_emitter.cpp
        PROPERTIES COMPILE_DEFINITIONS WOWEE_STB_IMAGE_WRITE_EXTERNAL)
endif()

# Lua 5.1 runtime.
set(LUA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/lua-5.1.5/src)
set(LUA_SOURCES
    ${LUA_DIR}/lapi.c    ${LUA_DIR}/lcode.c   ${LUA_DIR}/ldebug.c
    ${LUA_DIR}/ldo.c     ${LUA_DIR}/ldump.c   ${LUA_DIR}/lfunc.c
    ${LUA_DIR}/lgc.c     ${LUA_DIR}/llex.c    ${LUA_DIR}/lmem.c
    ${LUA_DIR}/lobject.c ${LUA_DIR}/lopcodes.c ${LUA_DIR}/lparser.c
    ${LUA_DIR}/lstate.c  ${LUA_DIR}/lstring.c ${LUA_DIR}/ltable.c
    ${LUA_DIR}/ltm.c     ${LUA_DIR}/lundump.c ${LUA_DIR}/lvm.c
    ${LUA_DIR}/lzio.c    ${LUA_DIR}/lauxlib.c ${LUA_DIR}/lbaselib.c
    ${LUA_DIR}/ldblib.c  ${LUA_DIR}/liolib.c  ${LUA_DIR}/lmathlib.c
    ${LUA_DIR}/loslib.c  ${LUA_DIR}/ltablib.c ${LUA_DIR}/lstrlib.c
    ${LUA_DIR}/linit.c
)
add_library(lua51 STATIC ${LUA_SOURCES})
set_target_properties(lua51 PROPERTIES LINKER_LANGUAGE C C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
target_include_directories(lua51 SYSTEM PUBLIC ${LUA_DIR})
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lua51 PRIVATE -w)
endif()

add_executable(wowee ${WOWEE_SOURCES} ${WOWEE_HEADERS} ${WOWEE_PLATFORM_SOURCES})
# Check the committed binaries, including their matching source hashes, on
# every build. Lighting changes must not silently ship an older shader.
add_custom_target(wowee_shader_verify
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/ps4/verify_shaders.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Verifying packaged lighting shader source/binary pairs"
)
add_dependencies(wowee wowee_shader_verify)

if(WOWEE_PS4)
    set_target_properties(wowee PROPERTIES
        OUTPUT_NAME wowee
        SUFFIX .elf
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    target_link_options(wowee PRIVATE --wrap=clock_gettime)
    target_link_options(wowee PRIVATE --wrap=realloc)
    target_link_options(wowee PRIVATE --wrap=__expand_heap)
    target_link_options(wowee PRIVATE
        --wrap=_ZNSt3__118condition_variable15__do_timed_waitERNS_11unique_lockINS_5mutexEEENS_6chrono10time_pointINS5_12system_clockENS5_8durationIxNS_5ratioILl1ELl1000000000EEEEEEE)
endif()
if(TARGET opcodes-generate)
    add_dependencies(wowee opcodes-generate)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(src/rendering/amd_fsr3_runtime.cpp PROPERTIES
        COMPILE_OPTIONS "-Wno-unknown-pragmas;-Wno-unused-variable"
    )
endif()

# Version information.
set(WOWEE_VERSION_HEADER ${CMAKE_BINARY_DIR}/generated/core/version.hpp)
set(WOWEE_VERSION_SCRIPT_ARGS
    -DSRC_DIR=${CMAKE_SOURCE_DIR}
    -DIN_FILE=${CMAKE_SOURCE_DIR}/include/core/version.hpp.in
    -DOUT_FILE=${WOWEE_VERSION_HEADER}
    -P ${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake
)
execute_process(COMMAND ${CMAKE_COMMAND} ${WOWEE_VERSION_SCRIPT_ARGS})
add_custom_target(wowee_version ALL
    COMMAND ${CMAKE_COMMAND} ${WOWEE_VERSION_SCRIPT_ARGS}
    BYPRODUCTS ${WOWEE_VERSION_HEADER}
    COMMENT "Resolving version from git"
    VERBATIM
)
add_dependencies(wowee wowee_version)

# Include directories and precompiled standard headers.
if(WOWEE_PS4)
    target_include_directories(wowee BEFORE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/platform/ps4
        ${CMAKE_CURRENT_SOURCE_DIR}/ps4/third_party/ps4_vulkan/include
        ${CMAKE_CURRENT_SOURCE_DIR}/ps4/compat
        ${CMAKE_CURRENT_SOURCE_DIR}/ps4/compat/cxx20
    )
    target_include_directories(wowee PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools/asset_extract)
endif()
target_include_directories(wowee PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)
set(WOWEE_PCH_STD
    <cstdint>
    <cstddef>
    <string>
    <vector>
    <memory>
    <unordered_map>
    <unordered_set>
    <map>
    <set>
    <deque>
    <array>
    <optional>
    <functional>
    <algorithm>
    <chrono>
    <mutex>
    <future>
)

target_precompile_headers(wowee PRIVATE
    ${WOWEE_PCH_STD}
    <glm/glm.hpp>
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core/logger.hpp
)
target_include_directories(wowee SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/extern
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/vk-bootstrap/src
)

# Native console libraries.
    set(WOWEE_PS4_SCE_LIBS
        -lSceSysmodule
        -lSceSystemService
        -lSceUserService
        -lScePad
        -lSceAudioOut
        -lSceNet
        -lSceNetCtl
        -lSceImeDialog
        -lSceCommonDialog
        -lSceMsgDialog
        -lSceGnmDriver
        -lSceVideoOut
        -lSceRtc
    )
    target_link_libraries(wowee PRIVATE
        Vulkan::Vulkan
        wowee_ps4_crypto
        storm
        ZLIB::ZLIB
        Threads::Threads
        lua51
        ${WOWEE_PS4_SCE_LIBS}
    )

target_link_libraries(wowee PRIVATE imgui vk-bootstrap glm::glm-header-only)

# Diagnostics and optimization.
set(WOWEE_WARNINGS_AS_ERRORS_DEFAULT OFF)
option(WOWEE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ${WOWEE_WARNINGS_AS_ERRORS_DEFAULT})
    target_compile_options(wowee PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(wowee PRIVATE -Wunused-const-variable)
    endif()
    if(WOWEE_WARNINGS_AS_ERRORS)
        target_compile_options(wowee PRIVATE -Werror)
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(wowee PRIVATE -Wno-odr)
        target_link_options(wowee PRIVATE -Wno-odr)
    endif()
    target_compile_options(wowee PRIVATE
        -fno-omit-frame-pointer
        $<$<CONFIG:Debug>:-g3 -Og>
        $<$<CONFIG:RelWithDebInfo>:-g>
    )
if(NOT MSVC)
    target_compile_options(wowee PRIVATE $<$<CONFIG:Release>:-O3>)
    target_compile_options(wowee PRIVATE $<$<CONFIG:Release>:-fvisibility=hidden -fvisibility-inlines-hidden>)
endif()

# Runtime files and installable PS4 package.
if(WOWEE_PS4)
    file(GLOB WOWEE_PS4_SPV_FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/*.spv)
    set(WOWEE_PS4_RUNTIME_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:wowee>/assets/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/local_realm
            $<TARGET_FILE_DIR:wowee>/assets/local_realm
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/Data
            $<TARGET_FILE_DIR:wowee>/Data)
    foreach(_spv ${WOWEE_PS4_SPV_FILES})
        get_filename_component(_spv_name ${_spv} NAME)
        list(APPEND WOWEE_PS4_RUNTIME_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_spv} $<TARGET_FILE_DIR:wowee>/assets/shaders/${_spv_name})
    endforeach()
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures)
        list(APPEND WOWEE_PS4_RUNTIME_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures
                $<TARGET_FILE_DIR:wowee>/assets/textures)
    endif()
    foreach(_f grass_biomes.json krayonload.png krayonsignin.png WoWPS.png orgrimmar-entrance.png)
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets/${_f})
            list(APPEND WOWEE_PS4_RUNTIME_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_CURRENT_SOURCE_DIR}/assets/${_f}
                    $<TARGET_FILE_DIR:wowee>/assets/${_f})
        endif()
    endforeach()
    add_custom_target(wowee_ps4_runtime_files ALL
        ${WOWEE_PS4_RUNTIME_COMMANDS}
        COMMENT "PS4: syncing assets/shaders/*.spv, assets/textures and Data to $<TARGET_FILE_DIR:wowee>"
        VERBATIM)
    add_dependencies(wowee_ps4_runtime_files wowee)
    set(WOWEE_PS4_PAID "0x3800000000000011")
    add_custom_command(TARGET wowee POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E env OO_PS4_TOOLCHAIN=${OO_PS4_TOOLCHAIN}
            ${WOWEE_PS4_TOOLS_DIR}/create-fself
            -in=$<TARGET_FILE:wowee>
            -out=$<TARGET_FILE_DIR:wowee>/wowee.oelf
            --eboot $<TARGET_FILE_DIR:wowee>/eboot.bin
            --paid ${WOWEE_PS4_PAID}
        BYPRODUCTS ${CMAKE_BINARY_DIR}/eboot.bin ${CMAKE_BINARY_DIR}/wowee.oelf
        COMMENT "create-fself: $<TARGET_FILE_NAME:wowee> -> eboot.bin"
        VERBATIM)
    find_program(WOWEE_PS4_BASH bash)
    if(NOT WOWEE_PS4_BASH)
        set(WOWEE_PS4_BASH bash)
    endif()
    add_custom_target(ps4pkg
        COMMAND ${CMAKE_COMMAND} -E env OO_PS4_TOOLCHAIN=${OO_PS4_TOOLCHAIN}
            ${WOWEE_PS4_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tools/ps4/package.sh ${CMAKE_BINARY_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Packaging PS4 .pkg from ${CMAKE_BINARY_DIR}"
        VERBATIM USES_TERMINAL)
    add_dependencies(ps4pkg wowee wowee_ps4_runtime_files copy_bundled_addons)
endif()

# Bundled addons.
add_custom_target(copy_bundled_addons ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:wowee>/addons
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/addons $<TARGET_FILE_DIR:wowee>/addons
    COMMAND ${CMAKE_COMMAND} -DDIR=$<TARGET_FILE_DIR:wowee>/addons
            -P ${CMAKE_SOURCE_DIR}/cmake/strip_saved_vars.cmake
    COMMENT "Copying bundled addons")
add_dependencies(copy_bundled_addons wowee)

message(STATUS "WoWPS: PlayStation 4 / OpenOrbis")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Toolchain: ${OO_PS4_TOOLCHAIN}")
message(STATUS "Package target: ps4pkg")
