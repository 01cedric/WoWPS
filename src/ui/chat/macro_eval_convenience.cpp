// evaluateMacroConditionals - convenience free function.
// Thin wrapper over MacroEvaluator with concrete adapters.
// Separate TU to avoid pulling Application/Renderer into macro_evaluator unit tests.
#include "ui/chat/macro_evaluator.hpp"
#include "ui/chat/game_state_adapter.hpp"
#include "ui/chat/input_modifier_adapter.hpp"
#include "game/game_handler.hpp"
#include "core/application.hpp"
#include "rendering/renderer.hpp"

namespace wowee { namespace ui {

std::string evaluateMacroConditionals(const std::string& rawArg,
                                      game::GameHandler& gameHandler,
                                      uint64_t& targetOverride) {
    auto* renderer = core::Application::getInstance().getRenderer();
    GameStateAdapter gs(gameHandler, renderer);
    InputModifierAdapter im;
    MacroEvaluator eval(gs, im);
    return eval.evaluate(rawArg, targetOverride);
}

} // namespace ui
} // namespace wowee
