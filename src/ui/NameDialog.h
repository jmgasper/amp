// Small modal-style prompt for a playlist name; delivers the template message with "name" added.
#pragma once
#include <Message.h>
#include <Messenger.h>
#include <TextControl.h>
#include <Window.h>

namespace amp {

class NameDialog : public BWindow {
public:
    NameDialog(BWindow* parent, const char* title, const char* label, const char* initial,
        BMessage* templateMessage, BMessenger target);
    void MessageReceived(BMessage* message) override;

private:
    BTextControl* fText;
    BMessage* fTemplate;
    BMessenger fTarget;
};

} // namespace amp
