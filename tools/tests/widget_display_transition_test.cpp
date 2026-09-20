#include "ui/widget_tree.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

// Exercise the production layout/state used before FrameXML resize callbacks.
int main() {
    using namespace wowee::ui;
    WidgetTree tree;
    assert(tree.consumeDisplayChanges() == 0);
    const auto panel = tree.create(WidgetKind::Frame, tree.uiParentId(), "BagColumn");
    tree.setWidth(panel, 200); tree.setHeight(panel, 300);
    tree.addPoint(panel, {"BOTTOMRIGHT", tree.uiParentId(), "BOTTOMRIGHT", -8, 80});
    const auto check = [&](float width, float height) {
        const auto* root = tree.get(tree.uiParentId());
        const auto* bag = tree.get(panel);
        const float scale = tree.uiScale();
        assert(std::isfinite(scale) && scale > 0);
        assert(std::abs((bag->left + bag->rectW) - (root->left + root->rectW - 8)) < .02f);
        assert(bag->left >= 0 && bag->bottom >= 0);
        assert((bag->left + bag->rectW) * scale <= width);
        assert((bag->bottom + bag->rectH) * scale <= height);
    };
    tree.layout(1920, 1080); assert(tree.consumeDisplayChanges() == 3); check(1920,1080);
    tree.layout(1920, 1080); assert(tree.consumeDisplayChanges() == 0);
    tree.layout(1280, 720); assert(tree.consumeDisplayChanges() == 3); check(1280,720);
    tree.setUserScale(.8f); tree.layout(1280,720);
    assert(tree.consumeDisplayChanges() == 2); check(1280,720);
    tree.setSafeAreaInset(.04f); tree.layout(1280,720);
    assert(tree.consumeDisplayChanges() == 2); check(1280,720);
    const float prior = tree.uiScale();
    tree.setUserScale(std::numeric_limits<float>::quiet_NaN());
    tree.setSafeAreaInset(std::numeric_limits<float>::infinity());
    tree.layout(0,0); tree.layout(std::numeric_limits<float>::infinity(),720);
    assert(tree.uiScale() == prior && tree.consumeDisplayChanges() == 0);
    tree.reset(); assert(tree.consumeDisplayChanges() == 0);
    tree.layout(1920,1080); assert(tree.consumeDisplayChanges() == 3);
    std::puts("PASS: display resize/scale/inset events, settled bag anchors, invalid geometry, reset");
}
