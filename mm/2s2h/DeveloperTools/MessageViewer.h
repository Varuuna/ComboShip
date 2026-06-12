#pragma once

#include "z64.h"

#ifdef __cplusplus
#include <ship/window/gui/GuiWindow.h>
#include <unordered_map>

extern "C" {
#endif

/**
 * \brief Displays a vanilla message in a text box on screen.
 * \param tableId Unused (reserved for future use)
 * \param textId The textId corresponding to the message to display
 * \param language Unused (reserved for future use)
 */
void MessageDebug_StartTextBox(const char* tableId, uint16_t textId, uint8_t language);

/**
 * \brief Displays a custom message using Custom Message Syntax.
 * \param customMessage A string using Custom Message Syntax.
 */
void MessageDebug_DisplayCustomMessage(const char* customMessage);

#ifdef __cplusplus
}

class MessageViewerWindow : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override;

    ~MessageViewerWindow() override;

  private:
    void DisplayExistingMessage() const;
    void DisplayCustomMessage() const;
    void LoadMessageToEditor();
    bool ParseTextIdFromBuffer(uint16_t& outTextId);

    static constexpr uint16_t MAX_STRING_SIZE = 1024;
    static constexpr int HEXADECIMAL = 0;
    static constexpr int DECIMAL = 1;

    // ComboShip: null-init — in combo the shared Gui rejects this window as a duplicate name (OOT
    // registers its own "Message Viewer" first), so InitElement() never allocates these and the
    // destructor would free 0xCD debug-heap garbage at MM_Deinit (the shutdown freeze).
    char* mTextIdBuf = nullptr;
    uint16_t mTextId;
    int mTextIdBase = HEXADECIMAL;
    char* mCustomMessageBuf = nullptr;
    std::string mCustomMessageString;
    bool mDisplayExistingMessageClicked = false;
    bool mDisplayCustomMessageClicked = false;
    bool mLoadMessageClicked = false;
};

#endif // __cplusplus
