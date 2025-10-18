#pragma once

#include <cstdint>

struct BonfireWarpParam {
    /* NT版出力から外すか
     *   ○をつけたパラメータをNT版パッケージでは除外します
     * Disable Param - Network Test
     *   Parameters marked with  are excluded in the NT version package. */
    uint8_t disableParam_NT:1;

    /* パッケージ出力用リザーブ1
     *   パッケージ出力用リザーブ1 */
    char disableParamReserve1:7;

    /* パッケージ出力用リザーブ2
     *   パッケージ出力用リザーブ2 */
    char disableParamReserve2[3];

    /* イベントフラグID
     *   解除条件イベントフラグID
     * Event Flag ID
     *   Release condition event flag ID */
    uint32_t eventflagId;

    /* 篝火エンティティID
     *   篝火エンティティID
     * Bonfire Entity ID
     *   Bonfire entity ID */
    uint32_t bonfireEntityId;

    /* パッド
     *   パディング。削除した旧作由来データが定義されてた場所 */
    char pad4[2];

    /* ソートID
     *   篝火ワープサブカテゴリソートID。同じサブカテゴリ内の並び順（昇順）を指定する
     * Sort ID
     *   Bonfire Warp Subcategory Sort ID. Specify the order (ascending order) in the same subcategory */
    uint16_t bonfireSubCategorySortId;

    /* ワープ禁止アイコンID
     *   ワープ禁止時のアイコンID
     * Warp Prohibition Icon ID
     *   Icon ID when warp is prohibited */
    uint16_t forbiddenIconId;

    /* 表示ズームステップ
     *   篝火を表示するズームステップ（一番ズームアウトした状態が0、ズームするごとに+1）。「《表示ズームステップ》≦ 現在のズームステップ 」のときに表示される。デフォルトは 0（常に表示）
     * Display Zoom Step
     *   Zoom step to display the bonfire (0 when zoomed out most, +1 for each zoom). Displayed when Display zoom step  Current zoom step. Default is 0 (always displayed) */
    uint8_t dispMinZoomStep;

    /* 選択可能ズームステップ
     *   篝火を選択及びスナップ可能なズームステップ（一番ズームアウトした状態が0、ズームするごとに+1）。「《選択可能ズームステップ》≦ 現在の拡大段階 」のときに選択及びスナップ可能。デフォルトは 0（常に選択・スナップ可能）
     * Selectable Zoom Step
     *   A zoom step that allows you to select and snap a bonfire (0 for the most zoomed out state, +1 for each zoom). Can be selected and snapped when Selectable zoom step  current enlargement stage. The default is 0 (always selectable / snappable) */
    uint8_t selectMinZoomStep;

    /* サブカテゴリID
     *   篝火ワープサブカテゴリパラメータID(-1:無効)。どのサブカテゴリに属するかを設定する。無効なら篝火一覧に表示されない
     * Subcategory ID
     *   Bonfire Warp Subcategory Parameter ID (-1: Invalid). Set which subcategory it belongs to. If it is invalid, it will not be displayed in the bonfire list.
     * Default Value  = -1 */
    int32_t bonfireSubCategoryId;

    /* クリア済イベントフラグID
     *   クリア済みイベントフラグID(0:常にクリア済み扱い)
     * Cleared Event Flag ID
     *   Cleared event flag ID (0: always treated as cleared) */
    uint32_t clearedEventFlagId;

    /* アイコンID
     *   アイコンID
     * Icon ID
     *   Icon ID */
    uint16_t iconId;

    /* 表示設定M00
     *   M00で表示するか
     * Display Setting M00
     *   Whether to display with M00 */
    uint8_t dispMask00:1;

    /* 表示設定M01
     *   M01で表示するか
     * Display Setting M01
     *   Whether to display with M01 */
    uint8_t dispMask01:1;

    /* Display Setting M02
     *   Whether to display with M02 */
    uint8_t dispMask02:1;

    /* パッド
     *   pad1:6 */
    char pad1:5;

    /* パッド
     *   pad2 */
    char pad2[1];

    /* エリア番号
     *   mAA_BB_CC_DD の AA 部分
     * Map Area
     *   AA part of mAA_BB_CC_DD */
    uint8_t areaNo;

    /* グリッドX番号
     *   mAA_BB_CC_DD の BB 部分
     * Map Block
     *   BB part of mAA_BB_CC_DD */
    uint8_t gridXNo;

    /* グリッドZ番号
     *   mAA_BB_CC_DD の CC 部分
     * Map Region
     *   CC part of mAA_BB_CC_DD */
    uint8_t gridZNo;

    /* パディング
     *   pad3 */
    char pad3[1];

    /* X座標
     *   X座標
     * Coordinate: X
     *   X coordinate */
    float posX;

    /* Y座標
     *   Y座標（使っていない）
     * Coordinate: Y
     *   Y coordinate (not used) */
    float posY;

    /* Z座標
     *   Z座標
     * Coordinate: Z
     *   Z coordinate */
    float posZ;

    /* テキストID
     *   表示するテキストID。無効値(-1)なら、何も表示しない
     * Text ID [1]
     *   The text ID to display. If the value is invalid (-1), nothing is displayed.
     * Default Value  = -1 */
    int32_t textId1;

    /* 出現イベントフラグID
     *   テキストの表示イベントフラグID。イベントフラグがOnなら表示する。無効なイベントフラグID(0)なら、On扱いされる
     * Text Enable Flag ID [1]
     *   Display text Event flag ID. Display if the event flag is On. If the event flag ID (0) is invalid, it will be treated as On. */
    uint32_t textEnableFlagId1;

    /* 非表示イベントフラグID
     *   テキストの非表示イベントフラグID。イベントフラグがOnなら表示しない。表示イベントフラグIDよりも優先される。無効なイベントフラグID(0)なら、Off扱いされる
     * Text Disable Flag ID [1]
     *   Hidden event flag ID of the text. If the event flag is On, it will not be displayed. It takes precedence over the display event flag ID. If the event flag ID (0) is invalid, it will be treated as Off. */
    uint32_t textDisableFlagId1;

    /* テキストID
     *   表示するテキストID。無効値(-2)なら、何も表示しない
     * Text ID [2]
     *   The text ID to display. If it is an invalid value (-2), nothing is displayed.
     * Default Value  = -1 */
    int32_t textId2;

    /* 出現イベントフラグID
     *   テキストの表示イベントフラグID。イベントフラグがOnなら表示する。無効なイベントフラグID(1)なら、On扱いされる
     * Text Enable Flag ID [2]
     *   Display text Event flag ID. Display if the event flag is On. If the event flag ID (1) is invalid, it will be treated as On. */
    uint32_t textEnableFlagId2;

    /* 非表示イベントフラグID
     *   テキストの非表示イベントフラグID。イベントフラグがOnなら表示しない。表示イベントフラグIDよりも優先される。無効なイベントフラグID(1)なら、Off扱いされる
     * Text Disable Flag ID [2]
     *   Hidden event flag ID of the text. If the event flag is On, it will not be displayed. It takes precedence over the display event flag ID. If the event flag ID (1) is invalid, it will be treated as Off. */
    uint32_t textDisableFlagId2;

    /* テキストID
     *   表示するテキストID。無効値(-3)なら、何も表示しない
     * Text ID [3]
     *   The text ID to display. If it is an invalid value (-3), nothing is displayed.
     * Default Value  = -1 */
    int32_t textId3;

    /* 出現イベントフラグID
     *   テキストの表示イベントフラグID。イベントフラグがOnなら表示する。無効なイベントフラグID(2)なら、On扱いされる
     * Text Enable Flag ID [3]
     *   Display text Event flag ID. Display if the event flag is On. If the event flag ID (2) is invalid, it will be treated as On. */
    uint32_t textEnableFlagId3;

    /* 非表示イベントフラグID
     *   テキストの非表示イベントフラグID。イベントフラグがOnなら表示しない。表示イベントフラグIDよりも優先される。無効なイベントフラグID(2)なら、Off扱いされる
     * Text Disable Flag ID [3]
     *   Hidden event flag ID of the text. If the event flag is On, it will not be displayed. It takes precedence over the display event flag ID. If the event flag ID (2) is invalid, it will be treated as Off. */
    uint32_t textDisableFlagId3;

    /* テキストID
     *   表示するテキストID。無効値(-4)なら、何も表示しない
     * Text ID [4]
     *   The text ID to display. If it is an invalid value (-4), nothing is displayed.
     * Default Value  = -1 */
    int32_t textId4;

    /* 出現イベントフラグID
     *   テキストの表示イベントフラグID。イベントフラグがOnなら表示する。無効なイベントフラグID(3)なら、On扱いされる
     * Text Enable Flag ID [4]
     *   Display text Event flag ID. Display if the event flag is On. If the event flag ID (3) is invalid, it will be treated as On. */
    uint32_t textEnableFlagId4;

    /* 非表示イベントフラグID
     *   テキストの非表示イベントフラグID。イベントフラグがOnなら表示しない。表示イベントフラグIDよりも優先される。無効なイベントフラグID(3)なら、Off扱いされる
     * Text Disable Flag ID [4]
     *   Hidden event flag ID of the text. If the event flag is On, it will not be displayed. It takes precedence over the display event flag ID. If the event flag ID (3) is invalid, it will be treated as Off. */
    uint32_t textDisableFlagId4;

    /* テキストID
     *   表示するテキストID。無効値(-5)なら、何も表示しない
     * Text ID [5]
     *   The text ID to display. If it is an invalid value (-5), nothing is displayed.
     * Default Value  = -1 */
    int32_t textId5;

    /* 出現イベントフラグID
     *   テキストの表示イベントフラグID。イベントフラグがOnなら表示する。無効なイベントフラグID(4)なら、On扱いされる
     * Text Enable Flag ID [5]
     *   Display text Event flag ID. Display if the event flag is On. If the event flag ID (4) is invalid, it will be treated as On. */
    uint32_t textEnableFlagId5;

    /* 非表示イベントフラグID
     *   テキストの非表示イベントフラグID。イベントフラグがOnなら表示しない。表示イベントフラグIDよりも優先される。無効なイベントフラグID(4)なら、Off扱いされる
     * Text Disable Flag ID [5]
     *   Hidden event flag ID of the text. If the event flag is On, it will not be displayed. It takes precedence over the display event flag ID. If the event flag ID (4) is invalid, it will be treated as Off. */
    uint32_t textDisableFlagId5;

    /* テキストID
     *   表示するテキストID。無効値(-6)なら、何も表示しない
     * Text ID [6]
     *   The text ID to display. If it is an invalid value (-6), nothing is displayed.
     * Default Value  = -1 */
    int32_t textId6;

    /* 出現イベントフラグID
     *   テキストの表示イベントフラグID。イベントフラグがOnなら表示する。無効なイベントフラグID(5)なら、On扱いされる
     * Text Enable Flag ID [6]
     *   Display text Event flag ID. Display if the event flag is On. If the event flag ID (5) is invalid, it will be treated as On. */
    uint32_t textEnableFlagId6;

    /* 非表示イベントフラグID
     *   テキストの非表示イベントフラグID。イベントフラグがOnなら表示しない。表示イベントフラグIDよりも優先される。無効なイベントフラグID(5)なら、Off扱いされる
     * Text Disable Flag ID [6]
     *   Hidden event flag ID of the text. If the event flag is On, it will not be displayed. It takes precedence over the display event flag ID. If the event flag ID (5) is invalid, it will be treated as Off. */
    uint32_t textDisableFlagId6;

    /* テキストID
     *   表示するテキストID。無効値(-7)なら、何も表示しない
     * Text ID [7]
     *   The text ID to display. If it is an invalid value (-7), nothing is displayed.
     * Default Value  = -1 */
    int32_t textId7;

    /* 出現イベントフラグID
     *   テキストの表示イベントフラグID。イベントフラグがOnなら表示する。無効なイベントフラグID(6)なら、On扱いされる
     * Text Enable Flag ID [7]
     *   Display text Event flag ID. Display if the event flag is On. If the event flag ID (6) is invalid, it will be treated as On. */
    uint32_t textEnableFlagId7;

    /* 非表示イベントフラグID
     *   テキストの非表示イベントフラグID。イベントフラグがOnなら表示しない。表示イベントフラグIDよりも優先される。無効なイベントフラグID(6)なら、Off扱いされる
     * Text Disable Flag ID [7]
     *   Hidden event flag ID of the text. If the event flag is On, it will not be displayed. It takes precedence over the display event flag ID. If the event flag ID (6) is invalid, it will be treated as Off. */
    uint32_t textDisableFlagId7;

    /* テキストID
     *   表示するテキストID。無効値(-8)なら、何も表示しない
     * Text ID [8]
     *   The text ID to display. If it is an invalid value (-8), nothing is displayed.
     * Default Value  = -1 */
    int32_t textId8;

    /* 出現イベントフラグID
     *   テキストの表示イベントフラグID。イベントフラグがOnなら表示する。無効なイベントフラグID(7)なら、On扱いされる
     * Text Enable Flag ID [8]
     *   Display text Event flag ID. Display if the event flag is On. If the event flag ID (7) is invalid, it will be treated as On. */
    uint32_t textEnableFlagId8;

    /* 非表示イベントフラグID
     *   テキストの非表示イベントフラグID。イベントフラグがOnなら表示しない。表示イベントフラグIDよりも優先される。無効なイベントフラグID(7)なら、Off扱いされる
     * Text Disable Flag ID [8]
     *   Hidden event flag ID of the text. If the event flag is On, it will not be displayed. It takes precedence over the display event flag ID. If the event flag ID (7) is invalid, it will be treated as Off. */
    uint32_t textDisableFlagId8;

    /* テキスト種別
     *   テキストの種別(地名,NPC名,...)
     * Text Type [1]
     *   Text type (place name, NPC name, ...) */
    uint8_t textType1;

    /* テキスト種別
     *   テキストの種別(地名,NPC名,...)
     * Text Type [2]
     *   Text type (place name, NPC name, ...) */
    uint8_t textType2;

    /* テキスト種別
     *   テキストの種別(地名,NPC名,...)
     * Text Type [3]
     *   Text type (place name, NPC name, ...) */
    uint8_t textType3;

    /* テキスト種別
     *   テキストの種別(地名,NPC名,...)
     * Text Type [4]
     *   Text type (place name, NPC name, ...) */
    uint8_t textType4;

    /* テキスト種別
     *   テキストの種別(地名,NPC名,...)
     * Text Type [5]
     *   Text type (place name, NPC name, ...) */
    uint8_t textType5;

    /* テキスト種別
     *   テキストの種別(地名,NPC名,...)
     * Text Type [6]
     *   Text type (place name, NPC name, ...) */
    uint8_t textType6;

    /* テキスト種別
     *   テキストの種別(地名,NPC名,...)
     * Text Type [7]
     *   Text type (place name, NPC name, ...) */
    uint8_t textType7;

    /* テキスト種別
     *   テキストの種別(地名,NPC名,...)
     * Text Type [8]
     *   Text type (place name, NPC name, ...) */
    uint8_t textType8;

    /* 点火前SFXダミポリID0
     *   篝火点火前にSFXを出すダミポリID
     * Bonfire Ignition - Dummy Poly ID [0]
     *   Damipoli ID that issues SFX before bonfire ignition
     * Default Value  = -1 */
    int32_t noIgnitionSfxDmypolyId_0;

    /* 点火前SFXID0
     *   篝火点火前に出すSFXID。点火したら消える。-1の場合はSFXを出さない。
     * Bonfire Ignition - SFX ID [0]
     *   SFX ID issued before bonfire ignition. It goes out when ignited. In case of -1, SFX is not issued.
     * Default Value  = -1 */
    int32_t noIgnitionSfxId_0;

    /* 点火前SFXダミポリID1
     *   篝火点火前にSFXを出すダミポリID
     * Bonfire Ignition - Dummy Poly ID [1]
     *   Damipoli ID that issues SFX before bonfire ignition
     * Default Value  = -1 */
    int32_t noIgnitionSfxDmypolyId_1;

    /* 点火前SFXID1
     *   篝火点火前に出すSFXID。点火したら消える。-1の場合はSFXを出さない。
     * Bonfire Ignition - SFX ID [1]
     *   SFX ID issued before bonfire ignition. It goes out when ignited. In case of -1, SFX is not issued.
     * Default Value  = -1 */
    int32_t noIgnitionSfxId_1;

    /* Text Enable Flag ID 2 [1]
     *   Display text Event flag ID. Display if the event flag is On. If the event flag ID (0) is invalid, it will be treated as On. */
    int32_t textEnableFlag2Id1;

    /* Text Enable Flag ID 2 [2]
     *   Display text Event flag ID. Display if the event flag is On. If the event flag ID (0) is invalid, it will be treated as On. */
    int32_t textEnableFlag2Id2;

    /* Text Enable Flag ID 2 [3]
     *   Display text Event flag ID. Display if the event flag is On. If the event flag ID (0) is invalid, it will be treated as On. */
    int32_t textEnableFlag2Id3;

    /* Text Enable Flag ID 2 [4]
     *   Display text Event flag ID. Display if the event flag is On. If the event flag ID (0) is invalid, it will be treated as On. */
    int32_t textEnableFlag2Id4;

    /* Text Enable Flag ID 2 [5]
     *   Display text Event flag ID. Display if the event flag is On. If the event flag ID (0) is invalid, it will be treated as On. */
    int32_t textEnableFlag2Id5;

    /* Text Enable Flag ID 2 [6]
     *   Display text Event flag ID. Display if the event flag is On. If the event flag ID (0) is invalid, it will be treated as On. */
    int32_t textEnableFlag2Id6;

    /* Text Enable Flag ID 2 [7]
     *   Display text Event flag ID. Display if the event flag is On. If the event flag ID (0) is invalid, it will be treated as On. */
    int32_t textEnableFlag2Id7;

    /* Text Enable Flag ID 2 [8]
     *   Display text Event flag ID. Display if the event flag is On. If the event flag ID (0) is invalid, it will be treated as On. */
    int32_t textEnableFlag2Id8;

    /* Text Disable Flag ID 2 [1]
     *   Hidden event flag ID of the text. If the event flag is On, it will not be displayed. It takes precedence over the display event flag ID. If the event flag ID (0) is invalid, it will be treated as Off. */
    int32_t textDisableFlag2Id1;

    /* Text Disable Flag ID 2 [2]
     *   Hidden event flag ID of the text. If the event flag is On, it will not be displayed. It takes precedence over the display event flag ID. If the event flag ID (0) is invalid, it will be treated as Off. */
    int32_t textDisableFlag2Id2;

    /* Text Disable Flag ID 2 [3]
     *   Hidden event flag ID of the text. If the event flag is On, it will not be displayed. It takes precedence over the display event flag ID. If the event flag ID (0) is invalid, it will be treated as Off. */
    int32_t textDisableFlag2Id3;

    /* Text Disable Flag ID 2 [4]
     *   Hidden event flag ID of the text. If the event flag is On, it will not be displayed. It takes precedence over the display event flag ID. If the event flag ID (0) is invalid, it will be treated as Off. */
    int32_t textDisableFlag2Id4;

    /* Text Disable Flag ID 2 [5]
     *   Hidden event flag ID of the text. If the event flag is On, it will not be displayed. It takes precedence over the display event flag ID. If the event flag ID (0) is invalid, it will be treated as Off. */
    int32_t textDisableFlag2Id5;

    /* Text Disable Flag ID 2 [6]
     *   Hidden event flag ID of the text. If the event flag is On, it will not be displayed. It takes precedence over the display event flag ID. If the event flag ID (0) is invalid, it will be treated as Off. */
    int32_t textDisableFlag2Id6;

    /* Text Disable Flag ID 2 [7]
     *   Hidden event flag ID of the text. If the event flag is On, it will not be displayed. It takes precedence over the display event flag ID. If the event flag ID (0) is invalid, it will be treated as Off. */
    int32_t textDisableFlag2Id7;

    /* Text Disable Flag ID 2 [8]
     *   Hidden event flag ID of the text. If the event flag is On, it will not be displayed. It takes precedence over the display event flag ID. If the event flag ID (0) is invalid, it will be treated as Off. */
    int32_t textDisableFlag2Id8;

    /* Alternate Icon ID
     *   Icon ID that applies when a Text Enable Flag ID is true and it's corrosponding Text Type is set to 1 */
    uint16_t altIconId;

    /* Alternate Warp Prohibition Icon ID
     *   Icon ID when warp is prohibited, when certain conditions are met */
    uint16_t altForbiddenIconId;
};
