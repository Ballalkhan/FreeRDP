#include <cassert>

#include <winpr/cast.h>

#include "sdl_selectlist.hpp"

static const Uint32 vpadding = 5;

SdlSelectList::SdlSelectList(const std::string& title, const std::vector<std::string>& labels)
    : _window(nullptr), _renderer(nullptr)
{
	const size_t widget_height = 50;
	const size_t widget_width = 600;

	const size_t total_height = labels.size() * (widget_height + vpadding) + vpadding;
	const size_t height = total_height + widget_height;
	assert(widget_width <= INT32_MAX);
	assert(height <= INT32_MAX);

	auto flags = WINPR_ASSERTING_INT_CAST(
	    uint32_t, SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_MOUSE_FOCUS | SDL_WINDOW_INPUT_FOCUS);
	auto rc = SDL_CreateWindowAndRenderer(static_cast<int>(widget_width), static_cast<int>(height),
	                                      flags, &_window, &_renderer);
	if (rc != 0)
		widget_log_error(rc, "SDL_CreateWindowAndRenderer");
	else
	{
		SDL_SetWindowTitle(_window, title.c_str());

		SDL_Rect rect = { 0, 0, widget_width, widget_height };
		for (auto& label : labels)
		{
			_list.emplace_back(_renderer, label, rect);
			rect.y += widget_height + vpadding;
		}

		const std::vector<int> buttonids = { INPUT_BUTTON_ACCEPT, INPUT_BUTTON_CANCEL };
		const std::vector<std::string> buttonlabels = { "accept", "cancel" };
		_buttons.populate(_renderer, buttonlabels, buttonids, widget_width,
		                  static_cast<Sint32>(total_height), static_cast<Sint32>(widget_width / 2),
		                  static_cast<Sint32>(widget_height));
		_buttons.set_highlight(0);
	}
}

SdlSelectList::~SdlSelectList()
{
	_list.clear();
	_buttons.clear();
	SDL_DestroyRenderer(_renderer);
	SDL_DestroyWindow(_window);
}

int SdlSelectList::run()
{
	int res = -2;
	ssize_t CurrentActiveTextInput = 0;
	bool running = true;

	if (!_window || !_renderer)
		return -2;
	try
	{
		while (running)
		{
			if (!clear_window(_renderer))
				throw;

			if (!update_text())
				throw;

			if (!_buttons.update(_renderer))
				throw;

			SDL_Event event = {};
			SDL_WaitEvent(&event);
			switch (event.type)
			{
				case SDL_KEYDOWN:
					switch (event.key.keysym.sym)
					{
						case SDLK_UP:
						case SDLK_BACKSPACE:
							if (CurrentActiveTextInput > 0)
								CurrentActiveTextInput--;
							else
								CurrentActiveTextInput =
								    WINPR_ASSERTING_INT_CAST(ssize_t, _list.size() - 1);
							break;
						case SDLK_DOWN:
						case SDLK_TAB:
						{
							if (CurrentActiveTextInput < 0)
								CurrentActiveTextInput = 0;
							else
								CurrentActiveTextInput++;

							const auto s = _list.size();
							if (s <= 0)
								CurrentActiveTextInput = 0;
							else
								CurrentActiveTextInput =
								    CurrentActiveTextInput % WINPR_ASSERTING_INT_CAST(ssize_t, s);
						}
						break;
						case SDLK_RETURN:
						case SDLK_RETURN2:
						case SDLK_KP_ENTER:
							running = false;
							res = WINPR_ASSERTING_INT_CAST(int, CurrentActiveTextInput);
							break;
						case SDLK_ESCAPE:
							running = false;
							res = INPUT_BUTTON_CANCEL;
							break;
						default:
							break;
					}
					break;
				case SDL_MOUSEMOTION:
				{
					ssize_t TextInputIndex = get_index(event.button);
					reset_mouseover();
					if (TextInputIndex >= 0)
					{
						auto& cur = _list[WINPR_ASSERTING_INT_CAST(size_t, TextInputIndex)];
						if (!cur.set_mouseover(_renderer, true))
							throw;
					}

					_buttons.set_mouseover(event.button.x, event.button.y);
				}
				break;
				case SDL_MOUSEBUTTONDOWN:
				{
					auto button = _buttons.get_selected(event.button);
					if (button)
					{
						running = false;
						if (button->id() == INPUT_BUTTON_CANCEL)
							res = INPUT_BUTTON_CANCEL;
						else
							res = static_cast<int>(CurrentActiveTextInput);
					}
					else
					{
						CurrentActiveTextInput = get_index(event.button);
					}
				}
				break;
				case SDL_QUIT:
					res = INPUT_BUTTON_CANCEL;
					running = false;
					break;
				default:
					break;
			}

			reset_highlight();
			if (CurrentActiveTextInput >= 0)
			{
				auto& cur = _list[WINPR_ASSERTING_INT_CAST(size_t, CurrentActiveTextInput)];
				if (!cur.set_highlight(_renderer, true))
					throw;
			}

			SDL_RenderPresent(_renderer);
		}
	}
	catch (...)
	{
		return -1;
	}
	return res;
}

ssize_t SdlSelectList::get_index(const SDL_MouseButtonEvent& button)
{
	const Sint32 x = button.x;
	const Sint32 y = button.y;
	for (size_t i = 0; i < _list.size(); i++)
	{
		auto& cur = _list[i];
		auto r = cur.rect();

		if ((x >= r.x) && (x <= r.x + r.w) && (y >= r.y) && (y <= r.y + r.h))
			return WINPR_ASSERTING_INT_CAST(ssize_t, i);
	}
	return -1;
}

bool SdlSelectList::update_text()
{
	for (auto& cur : _list)
	{
		if (!cur.update_text(_renderer))
			return false;
	}

	return true;
}

void SdlSelectList::reset_mouseover()
{
	for (auto& cur : _list)
	{
		cur.set_mouseover(_renderer, false);
	}
}

void SdlSelectList::reset_highlight()
{
	for (auto& cur : _list)
	{
		cur.set_highlight(_renderer, false);
	}
}
