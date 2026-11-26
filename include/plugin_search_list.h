#pragma once

// ============================================================
// plugin_search_list.h
//
// A filter box with a list of result rows under it, drawn through
// IModLoaderImGui: type to filter, Up/Down move the highlight, Enter
// picks the highlighted row, a click picks the clicked row.
//
// The helper holds the mechanics only.  The plugin keeps its own data,
// decides what a row looks like and what a pick does.
//
// Usage (plugin author):
//
//   static PluginSearchList::State g_list;
//   static std::vector<Entry> g_matches;
//
//   // Called after the filter box has been read this frame; returns the
//   // number of rows to show.
//   static int CountRows(void*, const char* filter) {
//       g_matches = FindEntries(filter);
//       return (int)g_matches.size();
//   }
//   // Called right after the row's Selectable, still on its line: call
//   // SameLine(0.0f, 0.0f) first, then draw the row content.
//   static void DrawRow(IModLoaderImGui* imgui, int row, void*) {
//       imgui->SameLine(0.0f, 0.0f);
//       imgui->Text(g_matches[row].name.c_str());
//   }
//
//   auto r = PluginSearchList::Draw(imgui, "##MyPlugin_Search", g_list,
//                                   CountRows, DrawRow, nullptr, "Search...");
//   if (r.clickedRow >= 0) Pick(g_matches[r.clickedRow]);
//   else if (r.submitted)  Pick(g_matches[g_list.highlight]);
//
// Call Draw once per frame from the render callback, inside the window
// that holds the list.  The helper keeps no keybind state: Up, Down and
// Enter are read through ImGui, because the modloader withholds keybinds
// while an ImGui text field has focus.
// ============================================================

#include "plugin_interface.h"
#include <cstddef>
#include <cfloat>

namespace PluginSearchList
{
    // One list.  Keep one State per list, alive across frames.
    struct State
    {
        int   highlight = 0;          // Row the Up/Down keys and Enter act on.
        bool  scrollPending = false;  // Set when a key moved the highlight; the
                                      // highlighted row is scrolled into view.
        bool  focusFilter = false;    // Set to true to give the filter box keyboard
                                      // focus on the next Draw (e.g. when the window opens).
        char  filter[256] = "";       // What the player has typed.
        float mouseX = 0.0f;          // Mouse position of the last Draw, used to tell
        float mouseY = 0.0f;          // a moving mouse from a still one.

        // Empties the filter and resets the highlight.
        void Clear()
        {
            filter[0] = '\0';
            highlight = 0;
            scrollPending = false;
        }
    };

    struct Result
    {
        int  clickedRow = -1;       // Row clicked this frame, or -1.  Drawing stops at it.
        bool submitted = false;     // Enter was pressed in the filter box and at least one
                                    // row exists; the row to pick is State::highlight.
        bool filterChanged = false; // The text changed this frame (the highlight is back at 0).
    };

    // Called after the filter box has been read this frame.  Rebuild the
    // plugin's matches from `filter` and return how many rows to show.
    // Return 0 for an empty filter to show the box alone.  The helper draws
    // a Separator before the rows only when the count is above 0.
    using CountFn = int (*)(void* ctx, const char* filter);

    // Draws one row's content.  Called right after the row's Selectable, so
    // the last item is still the Selectable (IsItemHovered, right-click
    // handlers and the like see the row).  Call SameLine(0.0f, 0.0f) first,
    // then draw the content.  The helper has pushed an ID for the row.
    using RowFn = void (*)(IModLoaderImGui* imgui, int row, void* ctx);

    // ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap
    constexpr int kSelectableFlags = (1 << 1) | (1 << 4);

    // Draws the filter box and the rows.
    //   imgui     : the loader's ImGui interface
    //   id        : the filter box's label; start it with "##" to hide it
    //   state     : this list's State
    //   countFn   : see CountFn
    //   rowFn     : see RowFn
    //   ctx       : passed through to countFn and rowFn
    //   hint      : grey text shown while the box is empty
    //   rowHeight : height of a row; 0 takes ImGui's default
    inline Result Draw(IModLoaderImGui* imgui, const char* id, State& state,
                       CountFn countFn, RowFn rowFn, void* ctx,
                       const char* hint = "", float rowHeight = 0.0f)
    {
        Result result;

        if (state.focusFilter)
        {
            imgui->SetKeyboardFocusHere(0);
            state.focusFilter = false;
        }

        imgui->SetNextItemWidth(-FLT_MIN);
        if (imgui->InputTextWithHint(id, hint, state.filter, sizeof(state.filter)))
        {
            state.highlight = 0;
            result.filterChanged = true;
        }

        // Enter is detected off the InputText widget rather than a keybind:
        // the modloader only forwards keys once no ImGui widget has keyboard
        // focus, and Enter defocuses the field, so a keybind would fire on a
        // second press.  ImGui's deactivated-after-edit flag is true the
        // instant the field submits.
        //
        // That flag also fires when a click steals focus from the box, which
        // happens on the mouse-press frame, one frame before a clicked row (a
        // Selectable, which reports on release) can return true.  Left
        // unguarded, clicking a row would pick the highlighted one instead.
        // So a deactivation caused by a mouse press is ignored and the row's
        // own click handles it.
        const bool deactivated = imgui->IsItemDeactivatedAfterEdit();
        const bool mousePressed = imgui->IsMouseClicked(0, false) || imgui->IsMouseClicked(1, false);
        const bool enter = deactivated && !mousePressed;

        float mouseX = 0.0f, mouseY = 0.0f;
        imgui->GetMousePos(&mouseX, &mouseY);
        const bool mouseMoved = mouseX != state.mouseX || mouseY != state.mouseY;
        state.mouseX = mouseX;
        state.mouseY = mouseY;

        // Down presses minus Up presses this frame, repeating while a key is
        // held.
        int arrowSteps = 0;
        if (imgui->IsWindowFocused(0))
            arrowSteps = (imgui->IsKeyPressed(VK_DOWN, true) ? 1 : 0)
                       - (imgui->IsKeyPressed(VK_UP, true) ? 1 : 0);

        const int rowCount = countFn ? countFn(ctx, state.filter) : 0;
        if (rowCount <= 0)
        {
            state.highlight = 0;
            return result;
        }

        state.highlight += arrowSteps;
        if (state.highlight < 0) state.highlight = 0;
        if (state.highlight > rowCount - 1) state.highlight = rowCount - 1;
        if (arrowSteps != 0)
            state.scrollPending = true;

        imgui->Separator();

        for (int row = 0; row < rowCount; ++row)
        {
            imgui->PushIDInt(row);
            const bool clicked = imgui->SelectableFull("##row", row == state.highlight,
                                                       kSelectableFlags, 0.0f, rowHeight);

            // A moving mouse pulls the highlight to the row under it.
            if (mouseMoved && imgui->IsItemHovered())
                state.highlight = row;
            // A row reached with the arrow keys is scrolled into view.
            if (state.scrollPending && row == state.highlight)
            {
                imgui->SetScrollHereY(0.5f);
                state.scrollPending = false;
            }

            if (rowFn)
                rowFn(imgui, row, ctx);
            imgui->PopID();

            if (clicked)
            {
                result.clickedRow = row;
                return result;
            }
        }

        result.submitted = enter;
        return result;
    }
}
