#pragma once

void gui_init(void);
void gui_handle_mouse(int mouse_x, int mouse_y, unsigned char mouse_buttons);
void gui_handle_keyboard(unsigned char keycode, unsigned char mods);
void gui_render_frame(int mouse_x, int mouse_y);
