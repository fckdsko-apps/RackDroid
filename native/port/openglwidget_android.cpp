/* GLES3 replacement for src/widget/OpenGlWidget.cpp.
 *
 * Upstream's default drawFramebuffer() renders a demo triangle with legacy
 * fixed-function GL (glBegin/glOrtho), which does not exist on GLES. Plugins
 * that use OpenGlWidget override drawFramebuffer() anyway (and desktop-GL
 * plugin code won't run on GLES either — see PORTING.md); here the default
 * just clears the framebuffer.
 */
#include <widget/OpenGlWidget.hpp>
#include <context.hpp>


namespace rack {
namespace widget {


void OpenGlWidget::step() {
	// Render every frame
	dirty = true;
	FramebufferWidget::step();
}


void OpenGlWidget::drawFramebuffer() {
	math::Vec fbSize = getFramebufferSize();
	glViewport(0.0, 0.0, fbSize.x, fbSize.y);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}


} // namespace widget
} // namespace rack
