#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <vector>

namespace mclite {

// One reusable settings screen rendered per config-tool section. Conversations
// (Contacts/Channels/Rooms) render read-only for now.
// CamelCase members on purpose — ALL-CAPS names (DEVICE, GPS, …) collide with
// Arduino/board macros.
enum class SettingsSection {
    Device, Radio, Display, Messaging, Sound, Gps, Battery, Security,
    Contacts, Channels, Rooms, CannedMessages
};

// Generic enum/string choice fields rendered by the shared btnmatrix picker.
enum class ChoiceField { LocationFormat, ShowTelemetry, LocationPrecision, RegionPreset, AdvertInterval, PathHashMode,
                         PermSettings };

// Simple bool config fields toggled by the shared lv_switch callback. The id is
// stashed in the switch's user_data so one callback maps to the right field.
enum class BoolField {
    SaveHistory = 1, RequestTelemetry, AutoTelemetry, CannedMessages, AllowMute, ShowHopCount,
    ShareContact, Reactions, PermConvMgmt, PermCompanion, GpsEnabled
};

// Steps in the on-device conversation-add flows (one shared text editor, walked
// step by step). Gated by permissions.conversation_management.
enum class ConvoField {
    ContactAlias, ContactKey,
    ChanHashName, ChanPrivName, ChanPrivPsk,
    RoomName, RoomKey, RoomPass
};

class SettingsScreen {
public:
    void create(lv_obj_t* parent);
    void setSection(SettingsSection s) { _section = s; }

    // Which canned-message list the editor is working on.
    enum class CannedTarget : uint8_t { Global, Contact, Channel, Room };
    // Open the canned-message list for a target. Always go through this rather
    // than showSettings(CannedMessages) directly, so the target is never stale
    // from a previous per-conversation visit.
    void openCannedList(CannedTarget t, int idx);
    void show();
    void hide();
    void tick();  // refreshes the live Heard-Adverts count on the Radio section

    lv_obj_t* obj() { return _screen; }

private:
    SettingsSection _section = SettingsSection::Device;
    lv_obj_t* _screen     = nullptr;
    lv_obj_t* _content    = nullptr;
    lv_obj_t* _backBtn    = nullptr;
    lv_obj_t* _titleLabel = nullptr;  // window title, updated per section in show()

    // Local input group for the open text editor (textarea + Save/Cancel). Owned
    // here so it's freed on close — the editors create their own group separate
    // from UIManager's modal group, which only manages the overlay.
    lv_group_t* _editorGroup = nullptr;

    // Theme picker overlay (canned-message-style btnmatrix).
    lv_obj_t* _themeOverlay = nullptr;
    lv_obj_t* _themeBtnm    = nullptr;
    void hideThemePicker();

    // Device name editor overlay
    lv_obj_t* _nameOverlay  = nullptr;
    lv_obj_t* _nameTextarea = nullptr;
#ifdef PLATFORM_TWATCH
    lv_obj_t* _nameKbd      = nullptr;
#endif
    void hideNameEditor();
    static void nameRowCb(lv_event_t* e);
    static void nameReadyCb(lv_event_t* e);

    // Region/scope editor overlay (mirrors the device-name editor). Target = which scope:
    // global radio.scope, a channel's, or a room's (the override lives in that entry's config).
    enum class ScopeTarget { Global, Channel, Room };
    lv_obj_t* _scopeOverlay  = nullptr;
    lv_obj_t* _scopeTextarea = nullptr;
    ScopeTarget _scopeTarget = ScopeTarget::Global;
    int        _scopeTargetIdx = -1;   // channel/room index when target != Global
#ifdef PLATFORM_TWATCH
    lv_obj_t* _scopeKbd      = nullptr;
#endif
    void openScopeEditor();                  // builds the editor for the current _scopeChannelIdx
    void hideScopeEditor();
    static void scopeRowCb(lv_event_t* e);   // global radio scope (Radio screen row)
    static void scopeReadyCb(lv_event_t* e);

    // ─── "Get scope from repeater" picker (issue #45) ───
    // Additive to manual entry: the scope textarea is always editable; this only offers
    // a shortcut to fill it from a nearby repeater's advertised region list. The editor
    // overlay stays open underneath; each stage is a single ModalDialog on top (the modal
    // group system does not stack), and returnToScopeEditor() re-attaches the editor group.
    // Zero-hop only: the picker lists direct-range (hops==0) repeaters, the only ones the
    // query can reach (see docs/known-issues). So no path/hop fields are needed here.
    struct ScopeRepeater { uint8_t pubKey[32]; String name; };
    std::vector<ScopeRepeater> _scopeRepeaters;   // snapshot for the repeater roller
    std::vector<String> _scopeNames;              // scope options for the scope roller
    lv_obj_t* _scopeReqDialog = nullptr;          // "requesting…" dialog while awaiting a reply
    uint32_t  _scopeReqExpiry = 0;                // millis() deadline (0 = no request pending)
    // Roller picker (own overlay/panel/group). The scope editor stays OPEN underneath; this
    // roller's dedicated group leaves the editor's _editorGroup intact for returnToScopeEditor().
    lv_obj_t*  _scopeRollerOverlay = nullptr;
    lv_obj_t*  _scopeRollerPanel   = nullptr;
    lv_obj_t*  _scopeRoller        = nullptr;
    lv_group_t* _scopeRollerGroup  = nullptr;
    enum class ScopePick { Repeater, Scope };
    ScopePick _scopePickPhase = ScopePick::Repeater;
    void onScopeFromRepeater();                   // "From repeater" button → repeater roller
    void openScopeRoller(const String& title, const std::vector<String>& options);
    void hideScopeRoller();
    static void scopeRollerOkCb(lv_event_t* e);
    void beginScopeRequest(const uint8_t* pubKey, const String& name);  // send + "requesting…"
    void returnToScopeEditor();                   // re-attach the editor input group after a picker/popup
    void scopeInfo(const String& msg);            // one-button info modal → returnToScopeEditor
public:
    // Called by UIManager when a repeater scope-list reply arrives (issue #45).
    void handleScopeListReply(const std::vector<String>& scopes);
private:

    // Boot text editor overlay
    lv_obj_t* _bootTextOverlay  = nullptr;
    lv_obj_t* _bootTextTextarea = nullptr;
#ifdef PLATFORM_TWATCH
    lv_obj_t* _bootTextKbd      = nullptr;
#endif
    void hideBootTextEditor();
    static void bootTextRowCb(lv_event_t* e);
    static void bootTextReadyCb(lv_event_t* e);

    // Language picker overlay
    lv_obj_t* _langOverlay = nullptr;
    lv_obj_t* _langBtnm    = nullptr;
    void hideLanguagePicker();
    static void languageRowCb(lv_event_t* e);

    // Security pickers/editor
    lv_obj_t* _lockModeBtnm = nullptr;
    lv_obj_t* _autoLockBtnm = nullptr;
    lv_obj_t* _pinOverlay   = nullptr;
    lv_obj_t* _pinTextarea = nullptr;
#ifdef PLATFORM_TWATCH
    lv_obj_t* _pinKbd      = nullptr;
#endif
    void hideLockModePicker();
    void hideAutoLockPicker();
    void hidePinEditor();
    static void lockModeRowCb(lv_event_t* e);
    static void lockModeChosenCb(lv_event_t* e);
    // Which secret the PIN editor is setting. Same editor, two backing stores --
    // the CannedTarget trick. security.pinCode unlocks the screen; security.adminPin
    // unlocks the Admin hub, and they are deliberately different secrets.
    enum class PinTarget : uint8_t { ScreenPin, AdminPin };
    PinTarget _pinTarget = PinTarget::ScreenPin;
    void openPinEditor(PinTarget target = PinTarget::ScreenPin);
    static void pinRowCb(lv_event_t* e);
    static void adminLockRowCb(lv_event_t* e);
    static void adminPinRowCb(lv_event_t* e);
    static void pinReadyCb(lv_event_t* e);
    static void autoLockRowCb(lv_event_t* e);
    static void autoLockChosenCb(lv_event_t* e);

    // Sound settings
    lv_obj_t* _sosKeywordOverlay  = nullptr;
    lv_obj_t* _sosKeywordTextarea = nullptr;
#ifdef PLATFORM_TWATCH
    lv_obj_t* _sosKeywordKbd      = nullptr;
#endif
    void hideSosKeywordEditor();
    static void sosKeywordRowCb(lv_event_t* e);
    static void sosKeywordReadyCb(lv_event_t* e);

    lv_obj_t* _sosRepeatSlider    = nullptr;
    lv_obj_t* _sosRepeatValLbl    = nullptr;
    lv_obj_t* _lowAlertSlider     = nullptr;
    lv_obj_t* _lowAlertValLbl     = nullptr;
    static void soundToggleCb(lv_event_t* e);
    static void lowAlertToggleCb(lv_event_t* e);

    // Radio section TX-power slider — reboots on leave. (Advert interval is a
    // picker, see ChoiceField::AdvertInterval.)
    lv_obj_t* _txPowerSlider   = nullptr;
    lv_obj_t* _txPowerValLbl   = nullptr;

    // Messaging section sliders.
    lv_obj_t* _maxHistorySlider = nullptr;
    lv_obj_t* _maxHistoryValLbl = nullptr;
    lv_obj_t* _maxRetriesSlider = nullptr;
    lv_obj_t* _maxRetriesValLbl = nullptr;

    // GPS section sliders.
    lv_obj_t* _clockOffsetSlider = nullptr;
    lv_obj_t* _clockOffsetValLbl = nullptr;
    lv_obj_t* _lastKnownSlider   = nullptr;
    lv_obj_t* _lastKnownValLbl   = nullptr;

    // Live Heard-Adverts count row (Radio section) — refreshed by tick().
    lv_obj_t* _heardCountLabel = nullptr;
    uint32_t  _heardCacheVersion = 0;
    static void heardAdvertsRowCb(lv_event_t* e);

    // Offgrid toggle (Radio section) — opens a reboot-confirm dialog.
    static void offgridRowCb(lv_event_t* e);

    // Generic bool switch + the shared toggle callback (BoolField via user_data).
    static void boolToggleCb(lv_event_t* e);
    static void gpsToggleCb(lv_event_t* e);  // GPS enable needs reboot to (de)init

    // Shared enum/string choice picker (roller). _choiceField selects the set.
    ChoiceField _choiceField   = ChoiceField::LocationFormat;
    lv_obj_t*   _choicePanel   = nullptr;  // modal container (overlay child)
    lv_obj_t*   _choiceRoller  = nullptr;
    lv_obj_t*   _choiceOverlay = nullptr;
    void openChoicePicker(ChoiceField f);
    void hideChoicePicker();
    static void choiceChosenCb(lv_event_t* e);
    static void permSettingsRowCb(lv_event_t* e);
    static void locFormatRowCb(lv_event_t* e);
    static void showTelemetryRowCb(lv_event_t* e);
    static void locPrecisionRowCb(lv_event_t* e);
    static void regionRowCb(lv_event_t* e);
    static void advertRowCb(lv_event_t* e);
    static void pathHashRowCb(lv_event_t* e);

    // Timezone text editor (GPS section).
    lv_obj_t* _timezoneOverlay  = nullptr;
    lv_obj_t* _timezoneTextarea = nullptr;
#ifdef PLATFORM_TWATCH
    lv_obj_t* _timezoneKbd      = nullptr;
#endif
    void hideTimezoneEditor();
    static void timezoneRowCb(lv_event_t* e);
    static void timezoneReadyCb(lv_event_t* e);

    // Per-section body builders (keep show() readable). Each appends to _content.
    void buildDevice();
    void buildRadio();
    void buildDisplay();
    void buildMessaging();
    void buildSound();
    void buildGps();
    void buildBattery();
    void buildSecurity();
    void buildConvoList();  // Contacts / Channels / Rooms (read-only, or add/remove when permitted)

    // On-device add/remove of conversations (permissions.conversation_management).
    // One shared text-editor overlay walked step-by-step per add flow; changes
    // write config immediately and apply on reboot (showConvoRebootConfirm).
    lv_obj_t* _convoOverlay  = nullptr;
    lv_obj_t* _convoTextarea = nullptr;
    lv_obj_t* _convoTitleLbl = nullptr;
    lv_obj_t* _convoGenBtn   = nullptr;  // "Generate" — private-channel PSK only
#ifdef PLATFORM_TWATCH
    lv_obj_t* _convoKbd      = nullptr;
#endif
    ConvoField _convoField   = ConvoField::ContactAlias;
    String _convPendName;   // alias / channel name / room name carried between steps
    String _convPendKey;    // room public key carried between steps
    bool convoManageEnabled() const;     // permission + canEdit gate

    // Vertical button-list modal (picker styling: MODAL_TEXT_WIDTH btnmatrix,
    // one button per row) — replaces narrow side-by-side lv_msgbox dialogs.
    enum class ConvoModal { None, AddChooser, DeleteConfirm, RebootConfirm, OffgridConfirm, PermLockConfirm,
                            AdminLockConfirm };

    // A permission change the user has chosen but not yet confirmed. Tightening
    // permissions cannot be undone from the device (the rows are gated at
    // basic=false, so they go read-only the moment it applies), hence the
    // confirm step. Because the rows are only editable while settings=="full",
    // any change reachable here is a tightening one.
    enum class PermField : uint8_t { None, Settings, ConvMgmt, Companion };
    PermField _permPending      = PermField::None;
    String    _permPendingValue;   // Settings only: the chosen "restricted"/"none"
    void applyPendingPermission();
    ConvoModal _convoModal = ConvoModal::None;
    lv_obj_t*  _convoModalBtnm = nullptr;
    void openButtonModal(ConvoModal purpose);
    void hideButtonModal();
    void onConvoModalChoice(lv_obj_t* dlg, int idx);

    void openConvoEditor(ConvoField f);  // build overlay, then setConvoField(f)
    void setConvoField(ConvoField f);    // retitle/clear textarea for the given step
    void hideConvoEditor();
    void showConvoRebootConfirm();       // "Reboot now / OK" after a config change
    static void convoAddRowCb(lv_event_t* e);     // "Add …" row → chooser / picker
    static void convoRowCb(lv_event_t* e);        // tap an entry → confirm delete
    static void convoEditorReadyCb(lv_event_t* e);// validate + advance/commit
    static void pskGenerateCb(lv_event_t* e);     // fill a random 32-hex PSK

    // Small row helpers used across sections (cut the boilerplate).
    lv_obj_t* addReadOnlyRow(const char* label, const String& value);
    lv_obj_t* addNavRow(const char* label, const String& value, lv_event_cb_t cb);
    void      addSwitchRow(const char* label, bool checked, lv_event_cb_t cb,
                           void* swUserData, bool rowClickable = true);
    lv_obj_t* addSliderRow(const char* label, lv_obj_t** sliderOut, lv_obj_t** valLblOut,
                           int32_t lo, int32_t hi, int32_t val, const String& valText);
    void addSectionHeader(const char* title);

    // Permission gate (permissions.settings): is a control editable now? `basic`
    // controls (brightness family, theme) stay editable in "restricted" mode;
    // everything else needs "full". "none" locks everything. The *-Gated helpers
    // render the interactive control when allowed, else a read-only value row.
    bool canEdit(bool basic) const;
    void addNavRowGated(const char* label, const String& value, lv_event_cb_t cb, bool basic);
    void addSwitchRowGated(const char* label, bool checked, lv_event_cb_t cb, void* ud, bool basic);
    void addSliderRowGated(const char* label, lv_obj_t** sliderOut, lv_obj_t** valLblOut,
                           int32_t lo, int32_t hi, int32_t val, const String& valText, bool basic);

    // Inline sliders for brightness / auto-dim / dim brightness / keyboard brightness
    lv_obj_t* _brightnessSlider     = nullptr;
    lv_obj_t* _brightnessValLbl     = nullptr;
    lv_obj_t* _autoDimSlider        = nullptr;
    lv_obj_t* _autoDimValLbl        = nullptr;
    lv_obj_t* _dimBrightnessSlider  = nullptr;
    lv_obj_t* _dimBrightnessValLbl  = nullptr;
    lv_obj_t* _kbdBrightnessSlider  = nullptr;
    lv_obj_t* _kbdBrightnessValLbl  = nullptr;
    static void inlineSliderChangedCb(lv_event_t* e);
    static void inlineSliderReleasedCb(lv_event_t* e);

    // Small helper to create a styled row container
    lv_obj_t* createRowContainer(lv_obj_t* parent);

    // Pending PIN during two-step confirmation
    String _pendingPin;
    // Explicit step flag rather than testing _pendingPin.length(): an empty PIN is
    // a legitimate entry (it clears the PIN), and using the string's emptiness as
    // the "already submitted once" marker made that case loop forever.
    bool      _pinAwaitRepeat = false;
    lv_obj_t* _pinTitleLbl    = nullptr;   // retitled "Repeat PIN" on the second step
    // Set when the PIN editor was opened automatically because Lock Mode was set
    // to "pin" without a usable PIN. If the user leaves without setting one, the
    // mode reverts rather than silently sitting at a setting that does nothing.
    bool _pinModePending = false;

    // Canned messages editor overlay. The same editor serves the global list and
    // the per-conversation overrides; _cannedTarget says which. The distinction
    // matters: for Global an empty list means "use the 8 built-in defaults" and
    // editing materialises them, whereas for a conversation an empty list means
    // "fall back to global" — materialising there would silently detach that
    // conversation from the global list.
    CannedTarget _cannedTarget    = CannedTarget::Global;
    int          _cannedTargetIdx = -1;   // index into contacts/channels/roomServers
    std::vector<String>* cannedList() const;   // nullptr if the target vanished
    void syncCannedToRuntime() const;          // push a contact edit into ContactStore
    String cannedTargetName() const;

    lv_obj_t* _cannedOverlay  = nullptr;
    lv_obj_t* _cannedTextarea = nullptr;
    lv_obj_t* _cannedDelBtn   = nullptr;
    int        _cannedEditIdx  = -1;
#ifdef PLATFORM_TWATCH
    lv_obj_t* _cannedKbd      = nullptr;
#endif
    void buildCannedList();
    void openCannedEditor(int idx, const String& text);
    void hideCannedEditor();
    static void cannedSectionRowCb(lv_event_t* e);
    static void cannedRowCb(lv_event_t* e);
    static void cannedAddRowCb(lv_event_t* e);
    static void cannedSaveCb(lv_event_t* e);
    static void cannedDeleteCb(lv_event_t* e);

    static void backBtnCb(lv_event_t* e);
    static void themeRowCb(lv_event_t* e);   // opens the theme picker (reboots to apply)
    static void emojiToggleCb(lv_event_t* e);
    static void screenshotsToggleCb(lv_event_t* e);
    static void showMemoryToggleCb(lv_event_t* e);
    static void menuButtonToggleCb(lv_event_t* e);  // T-Deck status-bar gear
};

}  // namespace mclite
