#include "ui/framexml_emitter.hpp"
#include "ui/interface_layout.hpp"
#include "ui/widget_tree.hpp"
#include "ui/settings_schema.hpp"
#include "ui/xml_parser.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

using namespace wowee::ui;

namespace {
void require(bool value, const std::string& message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message.c_str()); std::exit(1); }
}
void closeTo(float actual, float expected, const std::string& message) {
    require(std::isfinite(actual) && std::abs(actual - expected) < 0.02f,
            message + ": " + std::to_string(actual) + " != " + std::to_string(expected));
}

// The host only supplies widget calls. XML parsing, template emission, anchor
// resolution, visibility transitions and draw/hit ordering are production code.
// It deliberately does not implement automatic raising inside the fixture.
struct Host {
    WidgetTree tree;
    lua_State* L = luaL_newstate();
    Host() {
        luaL_openlibs(L);
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, create, 1);
        lua_setglobal(L, "__Create");
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, call, 1);
        lua_setglobal(L, "__Call");
        run(R"lua(
__WoweeTemplates={}; __WoweeTemplateTypes={}; __WoweeTemplateInherits={}
local methods={}
local mt={__index=methods}
local objects={}
local function wrap(id,name,parent)
  local object=setmetatable({wid=id,name=name,parent=parent},mt)
  objects[id]=object
  if name then _G[name]=object end
  return object
end
function CreateFrame(kind,name,parent)
  return wrap(__Create(kind,name,parent and parent.wid or 0),name,parent)
end
function methods:GetName() return self.name end
function methods:GetParent() return self.parent end
function methods:RegisterForDrag(...) self.dragButtons={...} end
function methods:SetScript(event,callback)
  self.scripts=self.scripts or {}; self.scripts[event]=callback
end
function methods:SetParent(parent)
  self.parent=parent; __Call('SetParent',self.wid,parent and parent.wid or 0)
end
function methods:CreateTexture(name,layer)
  local object=wrap(__Create('Texture',name,self.wid),name,self)
  object:SetDrawLayer(layer)
  return object
end
function methods:SetPoint(point,relative,relativePoint,x,y)
  if type(relative)=='string' then relative=_G[relative] end
  __Call('SetPoint',self.wid,point,relative and relative.wid or 0,relativePoint,x or 0,y or 0)
end
function methods:SetAllPoints(relative)
  if type(relative)=='string' then relative=_G[relative] end
  __Call('SetAllPoints',self.wid,relative and relative.wid or 0)
end
for _,method in ipairs({'SetSize','SetWidth','SetHeight','SetFrameStrata','SetFrameLevel',
    'SetToplevel','EnableMouse','SetMovable','SetID','SetDrawLayer','SetTexture',
    'SetTexCoord','SetVertexColor','SetAlpha','ClearAllPoints','Show','Hide','Raise'}) do
  local key=method
  methods[key]=function(self,...) return __Call(key,self.wid,...) end
end
function __WoweeMissingTemplate(name) error('Unexpected missing template '..name) end
function __WoweeFireOnLoad() end -- scripts are explicitly removed from this geometry fixture
)lua");
        lua_getglobal(L, "CreateFrame");
        lua_pushstring(L, "Frame"); lua_pushstring(L, "UIParent"); lua_pushnil(L);
        require(lua_pcall(L, 3, 1, 0) == 0, "create UIParent fixture"); lua_pop(L, 1);
    }
    ~Host() { lua_close(L); }
    static Host& self(lua_State* state) {
        return *static_cast<Host*>(lua_touserdata(state, lua_upvalueindex(1)));
    }
    static int create(lua_State* state) {
        auto& host = self(state);
        const std::string kind = luaL_checkstring(state, 1);
        const std::string name = luaL_optstring(state, 2, "");
        const auto parent = static_cast<uint32_t>(luaL_checknumber(state, 3));
        auto id = name == "UIParent" ? host.tree.uiParentId() :
            host.tree.create(kind == "Texture" ? WidgetKind::Texture : WidgetKind::Frame, parent, name);
        lua_pushnumber(state, id); return 1;
    }
    static int call(lua_State* state) {
        auto& tree = self(state).tree;
        const std::string method = luaL_checkstring(state, 1);
        const auto id = static_cast<uint32_t>(luaL_checknumber(state, 2));
        auto* w = tree.get(id);
        require(w != nullptr, "fixture widget exists");
        const float a = static_cast<float>(lua_tonumber(state, 3));
        const float b = static_cast<float>(lua_tonumber(state, 4));
        if (method == "SetSize") { tree.setWidth(id, a); tree.setHeight(id, b); }
        else if (method == "SetWidth") tree.setWidth(id, a);
        else if (method == "SetHeight") tree.setHeight(id, a);
        else if (method == "SetFrameStrata") {
            w->strata = parseStrata(luaL_checkstring(state, 3)); w->strataExplicit = true;
        } else if (method == "SetFrameLevel") { w->level = static_cast<int>(a); w->levelExplicit = true; }
        else if (method == "SetToplevel") w->topLevel = lua_toboolean(state, 3);
        else if (method == "EnableMouse") w->mouseEnabled = lua_toboolean(state, 3);
        else if (method == "SetMovable") w->movable = lua_toboolean(state, 3);
        else if (method == "SetDrawLayer") w->layer = parseDrawLayer(luaL_checkstring(state, 3));
        else if (method == "SetTexture") w->texturePath = luaL_checkstring(state, 3);
        else if (method == "SetAlpha") w->alpha = a;
        else if (method == "SetPoint") tree.addPoint(id, {
            luaL_checkstring(state, 3), static_cast<uint32_t>(luaL_checknumber(state, 4)),
            luaL_optstring(state, 5, luaL_checkstring(state, 3)),
            static_cast<float>(lua_tonumber(state, 6)), static_cast<float>(lua_tonumber(state, 7))});
        else if (method == "SetAllPoints") tree.setAllPoints(id, static_cast<uint32_t>(a));
        else if (method == "SetParent") tree.setParent(id, static_cast<uint32_t>(a));
        else if (method == "ClearAllPoints") tree.clearPoints(id);
        else if (method == "Show") tree.setShown(id, true);
        else if (method == "Hide") tree.setShown(id, false);
        else if (method == "Raise") tree.raise(id);
        // These attributes cannot affect geometry, visibility or draw ordering.
        else if (method != "SetID" && method != "SetTexCoord" && method != "SetVertexColor")
            return luaL_error(state, "unhandled fixture method %s", method.c_str());
        return 0;
    }
    void run(const std::string& source) {
        if (luaL_dostring(L, source.c_str())) {
            std::fprintf(stderr, "FAIL Lua fixture: %s\n", lua_tostring(L, -1)); std::exit(1);
        }
    }
    void emit(const XmlNode& root) {
        const auto output = emitFrameXml(root);
        require(output.warnings.empty(), "FrameXML emits without warnings");
        run(output.lua);
    }
    Widget& named(const char* name) {
        auto* widget = tree.findByName(name);
        require(widget != nullptr, std::string("missing widget ") + name);
        return *widget;
    }
};

XmlNode readXml(const std::string& path) {
    std::ifstream input(path);
    require(input.good(), "read retail fixture " + path);
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    XmlNode root; std::string error;
    require(parseXml(text, root, error), "parse retail fixture: " + error);
    return root;
}
const XmlNode& find(const XmlNode& root, const char* name) {
    if (root.attrOr("name", "") == name) return root;
    for (const auto& child : root.children) {
        if (child.attrOr("name", "") == name) return child;
        if (child.name == "Frames") {
            for (const auto& frame : child.children)
                if (frame.attrOr("name", "") == name) return frame;
        }
    }
    require(false, std::string("retail fixture node ") + name); return root;
}

// Keep the retail geometry, art, parent and frame attributes verbatim. Remove
// scripts and unrelated controls because their gameplay APIs are outside this
// layout test; the window template still runs through the actual emitter.
XmlNode geometryOnly(XmlNode node) {
    std::erase_if(node.children, [](const XmlNode& child) {
        return child.name != "Size" && child.name != "Anchors" && child.name != "Layers";
    });
    for (auto& layers : node.children) if (layers.name == "Layers") {
        for (auto& layer : layers.children)
            std::erase_if(layer.children, [](const XmlNode& child) { return child.name != "Texture"; });
    }
    return node;
}

size_t drawIndex(const WidgetTree& tree, uint32_t id) {
    const auto& order = tree.drawOrder();
    const auto at = std::find_if(order.begin(), order.end(), [id](const Widget* w) { return w->id == id; });
    require(at != order.end(), "widget is present in draw order");
    return static_cast<size_t>(at - order.begin());
}

void retailLayering(const std::string& retailRoot) {
    Host host;
    const auto mainXml = readXml(retailRoot + "/MainMenuBar.xml");
    const auto containerXml = readXml(retailRoot + "/ContainerFrame.xml");
    const auto& main = find(mainXml, "MainMenuBar");
    const auto& art = find(main, "MainMenuBarArtFrame");
    const auto& bagTemplate = find(containerXml, "ContainerFrameTemplate");
    require(!main.attr("frameStrata") && !art.attr("frameStrata"),
            "retail actionbar inherits UIParent's MEDIUM stratum");
    require(bagTemplate.attrOr("frameStrata", "") == "MEDIUM" && bagTemplate.attrBool("toplevel"),
            "retail bag template is MEDIUM and top level");
    XmlNode root; root.name = "Ui";
    auto mainGeometry = geometryOnly(main);
    XmlNode frames; frames.name = "Frames"; frames.children.push_back(geometryOnly(art));
    mainGeometry.children.push_back(frames);
    root.children = {mainGeometry, geometryOnly(bagTemplate), find(containerXml, "ContainerFrame1")};
    host.emit(root);
    auto& tree = host.tree;
    auto& bag = host.named("ContainerFrame1");
    auto& bar = host.named("MainMenuBarArtFrame");
    auto& bagArt = host.named("ContainerFrame1BackgroundTop");
    auto& gryphon = host.named("MainMenuBarRightEndCap");
    require(bag.strataExplicit && bag.strata == FrameStrata::Medium && bag.topLevel,
            "emitted ContainerFrame1 receives its template stratum and top-level flag");
    require(!bar.strataExplicit, "emitted actionbar retains inherited stratum");
    host.run("ContainerFrame1:SetSize(192,322); ContainerFrame1:SetPoint('BOTTOMRIGHT',UIParent,'BOTTOMRIGHT',0,0)");
    tree.layout(1920, 1080);
    require(!bag.visible, "retail bag starts hidden");
    host.run("ContainerFrame1:Show()");
    tree.layout(1920, 1080);
    require(bag.effStrata == FrameStrata::Medium && gryphon.effStrata == FrameStrata::Medium,
            "both sides use their real retail MEDIUM stratum");
    require(drawIndex(tree, gryphon.id) < drawIndex(tree, bagArt.id),
            "newly opened bag art paints above the deeper actionbar gryphon");
    require(drawIndex(tree, host.named("MainMenuBarTexture3").id) < drawIndex(tree, bagArt.id),
            "newly opened bag art paints above the actionbar artwork");
    const float x = std::max(bag.left, gryphon.left) + 4;
    const float y = std::max(bag.bottom, gryphon.bottom) + 4;
    require(x < std::min(bag.left + bag.rectW, gryphon.left + gryphon.rectW) &&
            y < std::min(bag.bottom + bag.rectH, gryphon.bottom + gryphon.rectH),
            "fixture really overlaps the bag and gryphon");
    require(tree.hitTest(x, y) == bag.id, "overlapping bag receives mouse input");
    const int openedLevel = bag.effLevel;
    for (int repeat = 0; repeat < 20; ++repeat) {
        host.run("ContainerFrame1:Show(); ContainerFrame1:Raise()");
        tree.layout(1920, 1080);
    }
    require(bag.effLevel == openedLevel, "repeated Show/Raise excludes the bag's own regions and does not inflate levels");
    host.run("ContainerFrame1:Hide(); ContainerFrame1:Show()");
    tree.layout(1920, 1080);
    require(drawIndex(tree, gryphon.id) < drawIndex(tree, bagArt.id), "reopened bag remains above the actionbar");
    std::puts("PASS retail FrameXML: inherited MEDIUM actionbar, top-level bag Show, gryphon draw order, hit order, stable raises");
}

void resolutionChanges() {
    require(kDefaultSafeAreaPercent == 0, "new installations default to the viewport edges");
    std::size_t settingCount = 0;
    const auto* settings = clientSettingsSchema(settingCount);
    const auto* safeArea = std::find_if(settings, settings + settingCount,
        [](const SettingDesc& setting) { return std::string(setting.key) == "safearea"; });
    require(safeArea != settings + settingCount &&
            safeArea->defaultValue == kDefaultSafeAreaPercent,
            "PS4 Reset Defaults keeps the same full-display safe area as a new install");
    require(migratedSafeAreaPercent(4, 161) == 0, "old automatic four-percent margin migrates once");
    for (const int custom : {0, 3, 7})
        require(migratedSafeAreaPercent(custom, 161) == custom, "migration preserves an existing custom safe area");
    require(migratedSafeAreaPercent(4, 162) == 4, "new explicit four-percent preference remains selected");
    WidgetTree tree;
    const auto make = [&](const std::string& name, const std::string& point, float x, float y) {
        const auto id = tree.create(WidgetKind::Frame, tree.uiParentId(), name);
        tree.setWidth(id, 140); tree.setHeight(id, 90);
        tree.get(id)->mouseEnabled = true;
        tree.addPoint(id, {point, tree.uiParentId(), point, x, y});
        return id;
    };
    const auto player = make("PlayerFrame", "TOPLEFT", 19, -4);
    const auto minimap = make("MinimapCluster", "TOPRIGHT", -16, -16);
    const auto chat = make("ChatFrame1", "BOTTOMLEFT", 0, 0);
    const auto bottomRight = make("RightEdge", "BOTTOMRIGHT", 0, 0);
    const auto bar = make("MainMenuBar", "BOTTOM", 0, 0);
    const int sizes[][2] = {{1280,720}, {1920,1080}, {1024,768}, {3440,1440}, {1920,1200}, {2560,1440}, {3840,2160}, {1280,720}};
    for (float scale : {0.75f, 1.0f, WidgetTree::kMaxUserScale}) {
        tree.setUserScale(scale);
        for (const auto& size : sizes) {
            tree.layout(size[0], size[1]);
            const float uiScale = tree.uiScale();
            const auto& root = *tree.get(tree.uiParentId());
            closeTo(root.left, 0, "UIParent left"); closeTo(root.bottom, 0, "UIParent bottom");
            closeTo(root.rectW * uiScale, size[0], "UIParent uses the current pixel width");
            closeTo(root.rectH * uiScale, size[1], "UIParent uses the current pixel height");
            const auto& p = *tree.get(player); const auto& m = *tree.get(minimap);
            const auto& c = *tree.get(chat); const auto& r = *tree.get(bottomRight);
            const auto& b = *tree.get(bar);
            closeTo(p.left * uiScale, 19 * uiScale, "player follows the left viewport edge");
            closeTo((p.bottom + p.rectH) * uiScale, size[1] - 4 * uiScale, "player follows the top viewport edge");
            closeTo((m.left + m.rectW) * uiScale, size[0] - 16 * uiScale, "minimap follows the right viewport edge");
            closeTo((m.bottom + m.rectH) * uiScale, size[1] - 16 * uiScale, "minimap follows the top viewport edge");
            closeTo(c.left, 0, "chat touches left"); closeTo(c.bottom, 0, "chat touches bottom");
            closeTo((r.left + r.rectW) * uiScale, size[0], "right frame touches right");
            closeTo(r.bottom, 0, "right frame touches bottom");
            closeTo((b.left + b.rectW * .5f) * uiScale, size[0] * .5f, "actionbar remains horizontally centered");
            closeTo(b.bottom, 0, "actionbar touches bottom");
            require(tree.hitTest(m.left + 1, m.bottom + 1) == minimap, "resized minimap hit rect follows its drawing");
        }
    }
    tree.setUserScale(1);
    tree.setSafeAreaInset(.06f);
    tree.layout(1920, 1080);
    closeTo(tree.get(chat)->left * tree.uiScale(), 1920 * .06f, "explicit custom safe area still applies horizontally");
    closeTo(tree.get(chat)->bottom * tree.uiScale(), 1080 * .06f, "explicit custom safe area still applies vertically");
    tree.setSafeAreaInset(0);
    tree.layout(1280, 720);
    closeTo(tree.get(chat)->left, 0, "clearing safe area restores the left edge after resize");
    closeTo(tree.get(chat)->bottom, 0, "clearing safe area restores the bottom edge after resize");
    std::puts("PASS layout: seven resolutions, 16:9/16:10/4:3/ultrawide, three UI scales, repeated resize, edge anchors, hit rects, explicit safe area");
}

void forwardAnchorDependencies() {
    WidgetTree tree;
    const auto bag = tree.create(WidgetKind::Frame, tree.uiParentId(), "ContainerFrame1");
    tree.setWidth(bag, 192); tree.setHeight(bag, 322);
    tree.addPoint(bag, {"BOTTOMRIGHT", tree.uiParentId(), "BOTTOMRIGHT", 0, 70});

    // Retail scripts create new backpack artwork after the original XML has
    // already created item buttons and the money footer. Their anchors can
    // therefore depend on siblings that appear later in the retained tree.
    const auto footer = tree.create(WidgetKind::Frame, bag, "MoneyFooter");
    tree.setWidth(footer, 100); tree.setHeight(footer, 12);
    uint32_t buttons[24], rows[6];
    for (auto& button : buttons) {
        button = tree.create(WidgetKind::Frame, bag, "");
        tree.setWidth(button, 37); tree.setHeight(button, 37);
    }
    for (int row = 5; row >= 0; --row) {
        rows[row] = tree.create(WidgetKind::Texture, bag, "");
        tree.setWidth(rows[row], 176); tree.setHeight(rows[row], 37);
    }
    tree.addPoint(rows[0], {"TOPLEFT", bag, "TOPLEFT", 8, -50});
    for (int row = 1; row < 6; ++row)
        tree.addPoint(rows[row], {"TOPLEFT", rows[row - 1], "BOTTOMLEFT", 0, -4});
    tree.addPoint(footer, {"TOPLEFT", rows[5], "BOTTOMLEFT", 0, -6});
    for (int row = 0; row < 6; ++row) for (int col = 0; col < 4; ++col)
        tree.addPoint(buttons[row * 4 + col], {"TOPLEFT", rows[row], "TOPLEFT", col * 41.0f, 0});

    const int sizes[][2] = {{1920,1080}, {1024,768}, {3440,1440}, {1280,720}};
    for (const auto& size : sizes) {
        // One pass must be enough: a stale first pass is visible on screen and
        // can be cached indefinitely when no other widget dirties the tree.
        tree.layout(size[0], size[1]);
        const auto& b = *tree.get(bag);
        for (int row = 0; row < 6; ++row) for (int col = 0; col < 4; ++col) {
            const auto& button = *tree.get(buttons[row * 4 + col]);
            closeTo(button.left, b.left + 8 + col * 41, "forward-anchored item column follows the resized bag");
            closeTo(button.bottom + button.rectH, b.bottom + b.rectH - 50 - row * 41,
                    "forward-anchored item row is current in the first layout pass");
            require(button.left >= b.left && button.left + button.rectW <= b.left + b.rectW &&
                    button.bottom >= b.bottom && button.bottom + button.rectH <= b.bottom + b.rectH,
                    "all 24 slots stay inside the moved bag");
        }
        const auto& money = *tree.get(footer);
        closeTo(money.bottom + money.rectH, b.bottom + b.rectH - 298,
                "money footer follows the last row on the first pass");
        closeTo(b.bottom, 70, "retail bag placement stays above the actionbar baseline");
    }
    tree.setHeight(bag, 363);
    tree.layout(1280, 720);
    closeTo(tree.get(footer)->bottom + tree.get(footer)->rectH,
            tree.get(bag)->bottom + tree.get(bag)->rectH - 298,
            "height changes invalidate a deferred row/footer dependency without resizing the viewport");
    std::puts("PASS dependencies: first-pass forward sibling anchors, all 24 bag slots, money footer, aspect-ratio and height changes");
}

void immediateRaisedHitOrder() {
    WidgetTree tree;
    const auto bag = tree.create(WidgetKind::Frame, tree.uiParentId(), "RaisedBag");
    tree.setWidth(bag, 192); tree.setHeight(bag, 322);
    tree.addPoint(bag, {"BOTTOMLEFT", tree.uiParentId(), "BOTTOMLEFT", 100, 100});
    tree.get(bag)->mouseEnabled = true; tree.get(bag)->topLevel = true;
    const auto item = tree.create(WidgetKind::Frame, bag, "RaisedBagItem");
    tree.setWidth(item, 37); tree.setHeight(item, 37);
    tree.addPoint(item, {"BOTTOMLEFT", bag, "BOTTOMLEFT", 8, 8});
    tree.get(item)->mouseEnabled = true;
    tree.get(item)->level = 4; tree.get(item)->levelExplicit = true;
    const auto peer = tree.create(WidgetKind::Frame, tree.uiParentId(), "OverlappingPeer");
    tree.setWidth(peer, 192); tree.setHeight(peer, 322);
    tree.addPoint(peer, {"BOTTOMLEFT", bag, "BOTTOMLEFT", 0, 0});
    tree.get(peer)->mouseEnabled = true;
    tree.get(peer)->level = 8; tree.get(peer)->levelExplicit = true;
    tree.layout(1920, 1080);
    require(tree.hitTest(110, 110) == peer, "higher peer initially receives the overlapping click");
    tree.raise(bag);
    // A gamepad can press and release within the same update. The release
    // sees the raised tree before the next renderer layout pass occurs.
    require(tree.hitTest(110, 110) == item, "raised item receives release before the next layout pass");
    tree.layout(1920, 1080);
    require(tree.hitTest(110, 110) == item, "immediate and post-layout raised hit order agree");
    std::puts("PASS input: raising a window updates explicit child hit priority before the next layout");
}

void hiddenAnchorDependency() {
    WidgetTree tree;
    const auto dependent = tree.create(WidgetKind::Frame, tree.uiParentId(), "EarlierDependent");
    tree.setWidth(dependent, 20); tree.setHeight(dependent, 20);
    const auto target = tree.create(WidgetKind::Frame, tree.uiParentId(), "LaterTarget");
    tree.setWidth(target, 100); tree.setHeight(target, 100);
    tree.addPoint(target, {"BOTTOMLEFT", tree.uiParentId(), "BOTTOMLEFT", 100, 100});
    tree.addPoint(dependent, {"BOTTOMLEFT", target, "TOPRIGHT", 10, 10});
    const auto child = tree.create(WidgetKind::Frame, target, "TargetChild");
    tree.setAllPoints(child, target);
    tree.get(child)->mouseEnabled = true; tree.get(child)->hasBackdrop = true;
    tree.layout(1920, 1080);
    require(tree.get(child)->visible && tree.hitTest(150, 150) == child, "dependency child begins visible and clickable");
    drawIndex(tree, child);
    tree.setShown(target, false);
    tree.layout(1920, 1080);
    require(!tree.get(child)->visible && !tree.get(child)->visibleChain,
            "resolving a hidden later dependency also hides its descendants");
    require(tree.hitTest(150, 150) == 0, "hidden dependency child cannot receive mouse input");
    require(std::none_of(tree.drawOrder().begin(), tree.drawOrder().end(),
                        [child](const Widget* w) { return w->id == child; }),
            "hidden dependency child is absent from drawing");
    std::puts("PASS visibility: hiding a forward anchor dependency prunes its previously visible children");
}
} // namespace

int main(int argc, char** argv) {
    require(argc == 2, "usage: widget_framexml_layout_test retail-FrameXML-directory");
    retailLayering(argv[1]);
    resolutionChanges();
    forwardAnchorDependencies();
    immediateRaisedHitOrder();
    hiddenAnchorDependency();
}
