#include "Theme.h"
#include <Bitmap.h>
#include <Font.h>
#include <GradientLinear.h>
#include <Shape.h>

namespace tasamp {

rgb_color Blend(rgb_color a, rgb_color b, float amount)
{
    rgb_color out;
    out.red = (uint8)(a.red + (b.red - a.red) * amount);
    out.green = (uint8)(a.green + (b.green - a.green) * amount);
    out.blue = (uint8)(a.blue + (b.blue - a.blue) * amount);
    out.alpha = 255;
    return out;
}

void FillVerticalGradient(BView* view, BRect rect, rgb_color top, rgb_color bottom)
{
    BGradientLinear gradient(BPoint(rect.left, rect.top), BPoint(rect.left, rect.bottom));
    gradient.AddColor(top, 0);
    gradient.AddColor(bottom, 255);
    view->FillRect(rect, gradient);
}

void FillRoundGradient(BView* view, BRect rect, float radius, rgb_color top, rgb_color bottom)
{
    BGradientLinear gradient(BPoint(rect.left, rect.top), BPoint(rect.left, rect.bottom));
    gradient.AddColor(top, 0);
    gradient.AddColor(bottom, 255);
    view->FillRoundRect(rect, radius, radius, gradient);
}

void DrawMABadge(BView* view, BPoint leftTop, float height)
{
    BFont font(be_bold_font);
    font.SetSize(height - 3);
    BFont old;
    view->GetFont(&old);
    view->SetFont(&font);
    float textWidth = view->StringWidth("MA");
    float width = textWidth + 6;
    BRect rect(leftTop.x, leftTop.y, leftTop.x + width, leftTop.y + height);
    font_height fh;
    font.GetHeight(&fh);
    view->SetHighColor(theme::kBadgeBackground);
    view->FillRoundRect(rect, 3, 3);
    view->SetHighColor(theme::kBadgeText);
    view->SetDrawingMode(B_OP_OVER);
    view->DrawString("MA", BPoint(rect.left + (width - textWidth) / 2,
        rect.top + (height - (fh.ascent + fh.descent)) / 2 + fh.ascent));
    view->SetFont(&old);
}

void DrawNoteIcon(BView* view, BRect rect, rgb_color color)
{
    // a simple eighth note that scales with the rect
    float w = rect.Width();
    float h = rect.Height();
    view->SetHighColor(color);
    view->SetPenSize(w * 0.09f);
    BRect head(rect.left + w * 0.18f, rect.top + h * 0.62f, rect.left + w * 0.52f, rect.top + h * 0.88f);
    view->FillEllipse(head);
    view->StrokeLine(BPoint(head.right - w * 0.04f, head.top + h * 0.12f), BPoint(head.right - w * 0.04f, rect.top + h * 0.12f));
    view->StrokeLine(BPoint(head.right - w * 0.04f, rect.top + h * 0.12f), BPoint(rect.left + w * 0.82f, rect.top + h * 0.30f));
    view->SetPenSize(1);
}

void DrawArtPlaceholder(BView* view, BRect rect)
{
    FillVerticalGradient(view, rect, theme::kArtPlaceholderTop, theme::kArtPlaceholderBottom);
    view->SetHighColor(180, 180, 180);
    view->StrokeRect(rect);
    BRect note = rect.InsetByCopy(rect.Width() * 0.25f, rect.Height() * 0.25f);
    DrawNoteIcon(view, note, theme::kArtPlaceholderNote);
}

void DrawBitmapFitted(BView* view, const BBitmap* bitmap, BRect rect)
{
    if (!bitmap) {
        DrawArtPlaceholder(view, rect);
        return;
    }
    BRect bounds = bitmap->Bounds();
    float scale = std::min((rect.Width() + 1) / (bounds.Width() + 1), (rect.Height() + 1) / (bounds.Height() + 1));
    float w = (bounds.Width() + 1) * scale;
    float h = (bounds.Height() + 1) * scale;
    BRect dest(0, 0, w - 1, h - 1);
    dest.OffsetTo(rect.left + (rect.Width() + 1 - w) / 2, rect.top + (rect.Height() + 1 - h) / 2);
    view->SetDrawingMode(B_OP_COPY);
    view->DrawBitmap(bitmap, bounds, dest, B_FILTER_BITMAP_BILINEAR);
}

BString TruncateToWidth(const BView* view, const char* text, float width)
{
    BString result(text);
    BFont font;
    view->GetFont(&font);
    font.TruncateString(&result, B_TRUNCATE_END, width);
    return result;
}

void DrawTruncated(BView* view, const char* text, BRect rect, alignment align, float inset)
{
    if (!text || !*text)
        return;
    BString truncated = TruncateToWidth(view, text, rect.Width() - inset * 2);
    font_height fh;
    view->GetFontHeight(&fh);
    float y = rect.top + (rect.Height() - (fh.ascent + fh.descent)) / 2 + fh.ascent;
    float x = rect.left + inset;
    float width = view->StringWidth(truncated.String());
    if (align == B_ALIGN_RIGHT)
        x = rect.right - inset - width;
    else if (align == B_ALIGN_CENTER)
        x = rect.left + (rect.Width() - width) / 2;
    view->SetDrawingMode(B_OP_OVER);
    view->DrawString(truncated.String(), BPoint(x, y));
}

} // namespace tasamp
