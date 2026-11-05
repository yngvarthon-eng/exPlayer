#pragma once

// Implemented by components that can sit in the top-right corner of a
// FullScreenHostWindow (the hosted content, or its side panel): the window's
// Exit button is drawn there, and `width` is how much of their top bar's right
// end to leave free for it (0 when the button isn't over them).
struct ReservesTopRightCorner
{
    virtual ~ReservesTopRightCorner() = default;
    virtual void setTopRightReserve(int width) = 0;
};
