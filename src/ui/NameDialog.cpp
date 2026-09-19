#include "NameDialog.h"
#include <Button.h>
#include <LayoutBuilder.h>
#include <Screen.h>

namespace tasamp {

namespace {
const uint32 kMsgOk = 'okay';
const uint32 kMsgCancel = 'cncl';
}

NameDialog::NameDialog(BWindow* parent, const char* title, const char* label, const char* initial,
    BMessage* templateMessage, BMessenger target)
    : BWindow(BRect(0, 0, 360, 110), title, B_FLOATING_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
        B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
      fTemplate(templateMessage), fTarget(target)
{
    fText = new BTextControl("name", label, initial, new BMessage(kMsgOk));
    BButton* ok = new BButton("ok", "OK", new BMessage(kMsgOk));
    BButton* cancel = new BButton("cancel", "Cancel", new BMessage(kMsgCancel));
    ok->MakeDefault(true);
    BLayoutBuilder::Group<>(this, B_VERTICAL, 10)
        .SetInsets(14, 14, 14, 14)
        .Add(fText)
        .AddGroup(B_HORIZONTAL)
            .AddGlue()
            .Add(cancel)
            .Add(ok)
        .End();
    if (parent) {
        BRect frame = parent->Frame();
        MoveTo(frame.left + (frame.Width() - 360) / 2, frame.top + 120);
    } else
        CenterOnScreen();
    fText->MakeFocus(true);
    fText->TextView()->SelectAll();
}

void NameDialog::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case kMsgOk: {
            BString name(fText->Text());
            name.Trim();
            if (name.IsEmpty())
                return;
            BMessage reply(*fTemplate);
            reply.AddString("name", name.String());
            fTarget.SendMessage(&reply);
            delete fTemplate;
            fTemplate = nullptr;
            PostMessage(B_QUIT_REQUESTED);
            break;
        }
        case kMsgCancel:
            delete fTemplate;
            fTemplate = nullptr;
            PostMessage(B_QUIT_REQUESTED);
            break;
        default:
            BWindow::MessageReceived(message);
    }
}

} // namespace tasamp
