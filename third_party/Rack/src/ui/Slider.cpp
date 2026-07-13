#include <ui/Slider.hpp>
#include <ui/Menu.hpp>


namespace rack {
namespace ui {


static const float SENSITIVITY = 0.001f;


Slider::Slider() {
	box.size.y = BND_WIDGET_HEIGHT;
}

void Slider::draw(const DrawArgs& args) {
	BNDwidgetState state = BND_DEFAULT;
	if (APP->event->hoveredWidget == this)
		state = BND_HOVER;
	if (APP->event->draggedWidget == this)
		state = BND_ACTIVE;

	float progress = quantity ? quantity->getScaledValue() : 0.f;
	std::string text = quantity ? quantity->getString() : "";

	// If parent is a Menu, make corners sharp
	ui::Menu* parentMenu = dynamic_cast<ui::Menu*>(getParent());
	int flags = parentMenu ? BND_CORNER_ALL : BND_CORNER_NONE;
	bndSlider(args.vg, 0.0, 0.0, box.size.x, box.size.y, flags, state, progress, text.c_str(), NULL);
}

void Slider::onDragStart(const DragStartEvent& e) {
	if (e.button != GLFW_MOUSE_BUTTON_LEFT)
		return;

	APP->window->cursorLock();
}

void Slider::onDragMove(const DragMoveEvent& e) {
	if (quantity) {
		quantity->moveScaledValue(SENSITIVITY * e.mouseDelta.x);
	}
}

void Slider::onDragEnd(const DragEndEvent& e) {
	APP->window->cursorUnlock();
}

void Slider::onDoubleClick(const DoubleClickEvent& e) {
	if (quantity)
		quantity->reset();
}


} // namespace ui
} // namespace rack
