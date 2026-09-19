/*
 * Windows interruptible console input implementation.
 *
 * The input thread blocks in WaitForMultipleObjects on:
 * - STD_INPUT_HANDLE;
 * - a private stop event.
 *
 * interrupt() signals the stop event so shutdown does not require user input.
 */
#include "console_input.hpp"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <array>
#include <string_view>

namespace cw::server {
namespace {

[[nodiscard]] std::string to_utf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (size <= 0) {
        return {};
    }

    std::string result(static_cast<std::size_t>(size), '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        size,
        nullptr,
        nullptr);

    return result;
}

void echo(HANDLE output, std::wstring_view value) {
    if (output == INVALID_HANDLE_VALUE ||
        output == nullptr ||
        value.empty()) {
        return;
    }

    DWORD written = 0;

    WriteConsoleW(
        output,
        value.data(),
        static_cast<DWORD>(value.size()),
        &written,
        nullptr);
}

}

struct console_input::implementation {
    // Process console input handle.
    HANDLE input = INVALID_HANDLE_VALUE;

    // Process console output handle used for interactive echo.
    HANDLE output = INVALID_HANDLE_VALUE;

    // Manual-reset event used to interrupt a blocked input wait.
    HANDLE stop_event = nullptr;

    // Current Unicode input line before UTF-8 conversion.
    std::wstring pending;

    // ReadConsoleInputW removes a whole batch from the console queue. Keep the
    // unread tail here so returning one completed line never drops later input.
    std::array<INPUT_RECORD, 32> records{};
    DWORD record_count = 0;
    DWORD record_position = 0;
};

console_input::console_input()
    : state(std::make_unique<implementation>()) {
}

console_input::~console_input() {
    close();
}

bool console_input::open() {
    // Re-open is intentionally idempotent for the current object lifetime.
    if (state->stop_event != nullptr) {
        return true;
    }

    state->input = GetStdHandle(STD_INPUT_HANDLE);
    state->output = GetStdHandle(STD_OUTPUT_HANDLE);

    if (state->input == INVALID_HANDLE_VALUE ||
        state->input == nullptr) {
        return false;
    }

    state->stop_event = CreateEventW(
        nullptr,
        TRUE,
        FALSE,
        nullptr);

    return state->stop_event != nullptr;
}

bool console_input::read_line(std::string& line) {
    HANDLE handles[2] = {
        state->input,
        state->stop_event,
    };

    while (true) {
        if (state->record_position >=
            state->record_count) {

            state->record_position = 0;
            state->record_count = 0;

            const DWORD result = WaitForMultipleObjects(
                2,
                handles,
                FALSE,
                INFINITE);

            if (result == WAIT_OBJECT_0 + 1) {
                return false;
            }

            if (result != WAIT_OBJECT_0) {
                return false;
            }

            if (!ReadConsoleInputW(
                    state->input,
                    state->records.data(),
                    static_cast<DWORD>(
                        state->records.size()),
                    &state->record_count)) {

                return false;
            }
        }

        while (state->record_position <
               state->record_count) {

            const auto& record =
                state->records[
                    state->record_position++];

            if (record.EventType != KEY_EVENT ||
                !record.Event.KeyEvent.bKeyDown) {
                continue;
            }

            const wchar_t ch =
                record.Event.KeyEvent.uChar.UnicodeChar;

            if (ch == L'\r') {
                echo(state->output, L"\r\n");

                line = to_utf8(state->pending);
                state->pending.clear();

                return true;
            }

            if (ch == L'\b') {
                if (!state->pending.empty()) {
                    state->pending.pop_back();
                    echo(state->output, L"\b \b");
                }

                continue;
            }

            if (ch >= L' ') {
                state->pending.push_back(ch);

                const wchar_t one[1] = {ch};

                echo(
                    state->output,
                    std::wstring_view(one, 1));
            }
        }
    }
}

void console_input::interrupt() noexcept {
    if (state->stop_event != nullptr) {
        SetEvent(state->stop_event);
    }
}

void console_input::close() noexcept {
    if (state->stop_event != nullptr) {
        CloseHandle(state->stop_event);
        state->stop_event = nullptr;
    }

    state->input = INVALID_HANDLE_VALUE;
    state->output = INVALID_HANDLE_VALUE;
    state->pending.clear();
    state->record_count = 0;
    state->record_position = 0;
}

}

#endif
