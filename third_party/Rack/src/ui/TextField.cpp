#include <ui/TextField.hpp>
#include <ui/MenuItem.hpp>
#include <helpers.hpp>
#include <context.hpp>

namespace rack {
namespace ui {


struct TextFieldCopyItem : ui::MenuItem {
	WeakPtr<TextField> textField;
	void onAction(const ActionEvent& e) override {
		if (!textField)
			return;
		textField->copyClipboard();
		APP->event->setSelectedWidget(textField);
	}
};


struct TextFieldCutItem : ui::MenuItem {
	WeakPtr<TextField> textField;
	void onAction(const ActionEvent& e) override {
		if (!textField)
			return;
		textField->cutClipboard();
		APP->event->setSelectedWidget(textField);
	}
};


struct TextFieldPasteItem : ui::MenuItem {
	WeakPtr<TextField> textField;
	void onAction(const ActionEvent& e) override {
		if (!textField)
			return;
		textField->pasteClipboard();
		APP->event->setSelectedWidget(textField);
	}
};


struct TextFieldSelectAllItem : ui::MenuItem {
	WeakPtr<TextField> textField;
	void onAction(const ActionEvent& e) override {
		if (!textField)
			return;
		textField->selectAll();
		APP->event->setSelectedWidget(textField);
	}
};


TextField::TextField() {
	box.size.y = BND_WIDGET_HEIGHT;
}

void TextField::draw(const DrawArgs& args) {
	nvgScissor(args.vg, RECT_ARGS(args.clipBox));

	BNDwidgetState state;
	if (this == APP->event->selectedWidget)
		state = BND_ACTIVE;
	else if (this == APP->event->hoveredWidget)
		state = BND_HOVER;
	else
		state = BND_DEFAULT;

	int begin = std::min(cursor, selection);
	int end = std::max(cursor, selection);

	std::string drawText;
	if (password) {
		drawText = std::string(string::UTF8Length(text), '*');
		begin = string::UTF8CodepointIndex(text, begin);
		end = string::UTF8CodepointIndex(text, end);
	}
	else {
		drawText = text;
	}

	bndTextField(args.vg, 0.0, 0.0, box.size.x, box.size.y, BND_CORNER_NONE, state, -1, drawText.c_str(), begin, end);

	// Draw placeholder text
	if (text.empty()) {
		bndIconLabelCaret(args.vg, 0.0, 0.0, box.size.x, box.size.y, -1, bndGetTheme()->textFieldTheme.itemColor, 13, placeholder.c_str(), bndGetTheme()->textFieldTheme.itemColor, 0, -1);
	}

	nvgResetScissor(args.vg);
}

void TextField::onDragHover(const DragHoverEvent& e) {
	OpaqueWidget::onDragHover(e);

	if (e.origin == this) {
		int pos = getTextPosition(e.pos);
		cursor = pos;
	}
}

void TextField::onButton(const ButtonEvent& e) {
	OpaqueWidget::onButton(e);

	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
		cursor = selection = getTextPosition(e.pos);
	}

	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
		createContextMenu();
		e.consume(this);
	}
}

void TextField::onSelectText(const SelectTextEvent& e) {
	std::u32string s32(1, char32_t(e.codepoint));
	std::string s8 = string::UTF32toUTF8(s32);
	insertText(s8);
	e.consume(this);
}

void TextField::onSelectKey(const SelectKeyEvent& e) {
#if defined ARCH_MAC
	#define TEXTFIELD_MOD_CTRL RACK_MOD_ALT
#else
	#define TEXTFIELD_MOD_CTRL RACK_MOD_CTRL
#endif

	if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
		// Backspace
		if (e.isKeyCommand(GLFW_KEY_BACKSPACE)) {
			if (cursor == selection) {
				cursor = string::UTF8PrevCodepoint(text, cursor);
			}
			insertText("");
			e.consume(this);
		}
		// Ctrl+Backspace
		if (e.isKeyCommand(GLFW_KEY_BACKSPACE, TEXTFIELD_MOD_CTRL)) {
			if (cursor == selection) {
				cursorToPrevWord();
			}
			insertText("");
			e.consume(this);
		}
		// Delete
		if (e.isKeyCommand(GLFW_KEY_DELETE)) {
			if (cursor == selection) {
				cursor = string::UTF8NextCodepoint(text, cursor);
			}
			insertText("");
			e.consume(this);
		}
		// Ctrl+Delete
		if (e.isKeyCommand(GLFW_KEY_DELETE, TEXTFIELD_MOD_CTRL)) {
			if (cursor == selection) {
				cursorToNextWord();
			}
			insertText("");
			e.consume(this);
		}
		// Left
		if (e.isKeyCommand(GLFW_KEY_LEFT)) {
			cursor = string::UTF8PrevCodepoint(text, cursor);
			selection = cursor;
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_LEFT, TEXTFIELD_MOD_CTRL)) {
			cursorToPrevWord();
			selection = cursor;
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_LEFT, GLFW_MOD_SHIFT)) {
			cursor = string::UTF8PrevCodepoint(text, cursor);
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_LEFT, TEXTFIELD_MOD_CTRL | GLFW_MOD_SHIFT)) {
			cursorToPrevWord();
			e.consume(this);
		}
		// Right
		if (e.isKeyCommand(GLFW_KEY_RIGHT)) {
			cursor = string::UTF8NextCodepoint(text, cursor);
			selection = cursor;
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_RIGHT, TEXTFIELD_MOD_CTRL)) {
			cursorToNextWord();
			selection = cursor;
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_RIGHT, GLFW_MOD_SHIFT)) {
			cursor = string::UTF8NextCodepoint(text, cursor);
			e.consume(this);
		}
		if (e.isKeyCommand(GLFW_KEY_RIGHT, TEXTFIELD_MOD_CTRL | GLFW_MOD_SHIFT)) {
			cursorToNextWord();
			e.consume(this);
		}
		// Up (placeholder)
		if (e.isKeyCommand(GLFW_KEY_UP)) {
			e.consume(this);
		}
		// Down (placeholder)
		if (e.isKeyCommand(GLFW_KEY_DOWN)) {
			e.consume(this);
		}
		// Home
		if (e.isKeyCommand(GLFW_KEY_HOME)
#if defined ARCH_MAC
			|| e.isKeyCommand(GLFW_KEY_LEFT, RACK_MOD_CTRL)
#endif
		) {
			selection = cursor = 0;
			e.consume(this);
		}
		// Shift+Home
		if (e.isKeyCommand(GLFW_KEY_HOME, GLFW_MOD_SHIFT)
#if defined ARCH_MAC
			|| e.isKeyCommand(GLFW_KEY_LEFT, RACK_MOD_CTRL | GLFW_MOD_SHIFT)
#endif
		) {
			cursor = 0;
			e.consume(this);
		}
		// End
		if (e.isKeyCommand(GLFW_KEY_END)
#if defined ARCH_MAC
			|| e.isKeyCommand(GLFW_KEY_RIGHT, RACK_MOD_CTRL)
#endif
		) {
			selection = cursor = text.size();
			e.consume(this);
		}
		// Shift+End
		if (e.isKeyCommand(GLFW_KEY_END, GLFW_MOD_SHIFT)
#if defined ARCH_MAC
			|| e.isKeyCommand(GLFW_KEY_RIGHT, RACK_MOD_CTRL | GLFW_MOD_SHIFT)
#endif
		) {
			cursor = text.size();
			e.consume(this);
		}
		// Ctrl+V
		if (e.isKeyCommand(GLFW_KEY_V, RACK_MOD_CTRL)) {
			pasteClipboard();
			e.consume(this);
		}
		// Ctrl+X
		if (e.isKeyCommand(GLFW_KEY_X, RACK_MOD_CTRL)) {
			cutClipboard();
			e.consume(this);
		}
		// Ctrl+C
		if (e.isKeyCommand(GLFW_KEY_C, RACK_MOD_CTRL)) {
			copyClipboard();
			e.consume(this);
		}
		// Ctrl+A
		if (e.isKeyCommand(GLFW_KEY_A, RACK_MOD_CTRL)) {
			selectAll();
			e.consume(this);
		}
		// Enter
		if (e.isKeyCommand(GLFW_KEY_ENTER) || e.isKeyCommand(GLFW_KEY_KP_ENTER)) {
			if (multiline) {
				insertText("\n");
			}
			else {
				ActionEvent eAction;
				onAction(eAction);
			}
			e.consume(this);
		}
		// Tab
		if (e.isKeyCommand(GLFW_KEY_TAB)) {
			if (nextField)
				APP->event->setSelectedWidget(nextField);
			e.consume(this);
		}
		// Shift+Tab
		if (e.isKeyCommand(GLFW_KEY_TAB, GLFW_MOD_SHIFT)) {
			if (prevField)
				APP->event->setSelectedWidget(prevField);
			e.consume(this);
		}
		// Consume all printable keys unless Ctrl is held
		if (GLFW_KEY_SPACE <= e.key && e.key < 128 && (e.mods & RACK_MOD_CTRL) == 0) {
			e.consume(this);
		}

		assert(0 <= cursor);
		assert(cursor <= (int) text.size());
		assert(0 <= selection);
		assert(selection <= (int) text.size());
	}
}

int TextField::getTextPosition(math::Vec mousePos) {
	return bndTextFieldTextPosition(APP->window->vg, 0.0, 0.0, box.size.x, box.size.y, -1, text.c_str(), mousePos.x, mousePos.y);
}

std::string TextField::getText() {
	return text;
}

void TextField::setText(std::string text) {
	if (this->text != text) {
		this->text = text;
		// ChangeEvent
		ChangeEvent eChange;
		onChange(eChange);
	}
	selection = cursor = text.size();
}

void TextField::selectAll() {
	cursor = text.size();
	selection = 0;
}

std::string TextField::getSelectedText() {
	int begin = std::min(cursor, selection);
	int len = std::abs(selection - cursor);
	return text.substr(begin, len);
}

void TextField::insertText(std::string text) {
	// Rack uses UNIX newlines so remove all CR characters
	text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());

	bool changed = false;
	if (cursor != selection) {
		// Delete selected text
		int begin = std::min(cursor, selection);
		int len = std::abs(selection - cursor);
		this->text.erase(begin, len);
		cursor = selection = begin;
		changed = true;
	}
	if (!text.empty()) {
		this->text.insert(cursor, text);
		cursor += text.size();
		selection = cursor;
		changed = true;
	}
	if (changed) {
		ChangeEvent eChange;
		onChange(eChange);
	}
}

void TextField::copyClipboard() {
	if (cursor == selection)
		return;
	glfwSetClipboardString(APP->window->win, getSelectedText().c_str());
}

void TextField::cutClipboard() {
	copyClipboard();
	insertText("");
}

void TextField::pasteClipboard() {
	const char* newText = glfwGetClipboardString(APP->window->win);
	if (!newText)
		return;
	insertText(newText);
}

void TextField::cursorToPrevWord() {
	if (password) {
		cursor = 0;
		return;
	}
	// This works for valid UTF-8 text
	size_t pos = text.rfind(' ', std::max(cursor - 2, 0));
	if (pos == std::string::npos)
		cursor = 0;
	else
		cursor = std::min((int) pos + 1, (int) text.size());
}

void TextField::cursorToNextWord() {
	if (password) {
		cursor = text.size();
		return;
	}
	// This works for valid UTF-8 text
	size_t pos = text.find(' ', std::min(cursor + 1, (int) text.size()));
	if (pos == std::string::npos)
		pos = text.size();
	cursor = pos;
}

void TextField::createContextMenu() {
	ui::Menu* menu = createMenu();

	TextFieldCutItem* cutItem = new TextFieldCutItem;
	cutItem->text = string::translate("TextField.cut");
	cutItem->rightText = widget::getKeyCommandName(GLFW_KEY_X, RACK_MOD_CTRL);
	cutItem->textField = this;
	menu->addChild(cutItem);

	TextFieldCopyItem* copyItem = new TextFieldCopyItem;
	copyItem->text = string::translate("TextField.copy");
	copyItem->rightText = widget::getKeyCommandName(GLFW_KEY_C, RACK_MOD_CTRL);
	copyItem->textField = this;
	menu->addChild(copyItem);

	TextFieldPasteItem* pasteItem = new TextFieldPasteItem;
	pasteItem->text = string::translate("TextField.paste");
	pasteItem->rightText = widget::getKeyCommandName(GLFW_KEY_V, RACK_MOD_CTRL);
	pasteItem->textField = this;
	menu->addChild(pasteItem);

	TextFieldSelectAllItem* selectAllItem = new TextFieldSelectAllItem;
	selectAllItem->text = string::translate("TextField.selectAll");
	selectAllItem->rightText = widget::getKeyCommandName(GLFW_KEY_A, RACK_MOD_CTRL);
	selectAllItem->textField = this;
	menu->addChild(selectAllItem);
}


} // namespace ui
} // namespace rack
