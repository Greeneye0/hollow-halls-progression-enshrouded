#pragma once
// Player-visible text in every language Enshrouded supports. This file is UTF-8; build with /utf-8
// so the literals stay UTF-8. The language follows the game's own setting (see notice::Language),
// or [Messages] Language in hollow-halls-progression.ini. The beacons' name in the map menu is the
// game's own text for "Return Beacon" when it can be read; ReturnBeacon below is the fallback.
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace hhp::text {

enum class Lang : uint8_t { en, fr, it, de, es, ja, ko, pl, pt, ru, zhHans, zhHant, tr, uk, th, count };
inline constexpr const char* kCodes[] = {"en", "fr", "it", "de", "es", "ja", "ko", "pl", "pt", "ru", "zh-hans", "zh-hant", "tr", "uk", "th"};

enum Id {
    Title, ReturnBeacon,
    Reached,    // two %u: the beacon's number, the hall's beacon count
    Travelling, // one %u: the beacon's number
    Locked, Busy, Checking, Missing, Failed, Reset,
    LoadingPercent, // the loading screen's progress, printf with the percent (0-100)
    IdCount
};

inline constexpr const char* kTexts[size_t(Lang::count)][IdCount] = {
// en
{"Hollow Halls Progression",
 "Return Beacon",
 "Return Beacon %u of %u reached. Click this dungeon on the world map to travel back here later.",
 "Travelling to Return Beacon %u...",
 "You have not reached that Return Beacon yet.",
 "A beacon journey is already under way.",
 "Still checking whether this server runs Hollow Halls Progression. Please try again in a few seconds.",
 "This server does not run Hollow Halls Progression, so beacon travel is unavailable here. The server needs the mod installed too.",
 "The journey to the Return Beacon could not be completed.",
 "Boss defeated. Your Return Beacons in this dungeon have been reset.",
 "Loading… %d%%"},
// fr
{"Hollow Halls Progression",
 "Balise de retour",
 "Balise de retour %u sur %u atteinte. Cliquez sur ce donjon sur la carte du monde pour revenir ici plus tard.",
 "Voyage vers la balise de retour %u...",
 "Vous n'avez pas encore atteint cette balise de retour.",
 "Un voyage vers une balise est déjà en cours.",
 "Vérification en cours : ce serveur utilise-t-il Hollow Halls Progression ? Veuillez réessayer dans quelques secondes.",
 "Ce serveur n'utilise pas Hollow Halls Progression : le voyage vers les balises n'est pas disponible ici. Le mod doit aussi être installé sur le serveur.",
 "Le voyage vers la balise de retour n'a pas pu aboutir.",
 "Boss vaincu. Vos balises de retour de ce donjon ont été réinitialisées.",
 "Chargement… %d %%"},
// it
{"Hollow Halls Progression",
 "Faro di ritorno",
 "Faro di ritorno %u di %u raggiunto. Fai clic su questo dungeon nella mappa del mondo per tornare qui in seguito.",
 "Viaggio verso il faro di ritorno %u...",
 "Non hai ancora raggiunto quel faro di ritorno.",
 "Un viaggio verso un faro è già in corso.",
 "Verifica in corso: questo server usa Hollow Halls Progression? Riprova tra qualche secondo.",
 "Questo server non usa Hollow Halls Progression, quindi il viaggio verso i fari non è disponibile qui. Anche il server deve avere la mod installata.",
 "Il viaggio verso il faro di ritorno non è riuscito.",
 "Boss sconfitto. I tuoi fari di ritorno in questo dungeon sono stati azzerati.",
 "Caricamento… %d%%"},
// de
{"Hollow Halls Progression",
 "Rückkehrleuchtfeuer",
 "Rückkehrleuchtfeuer %u von %u erreicht. Klicke auf der Weltkarte auf diesen Dungeon, um später hierher zurückzukehren.",
 "Reise zum Rückkehrleuchtfeuer %u ...",
 "Dieses Rückkehrleuchtfeuer hast du noch nicht erreicht.",
 "Eine Reise zu einem Leuchtfeuer läuft bereits.",
 "Es wird noch geprüft, ob dieser Server Hollow Halls Progression verwendet. Bitte versuche es in ein paar Sekunden erneut.",
 "Dieser Server verwendet Hollow Halls Progression nicht, daher ist die Reise zu Leuchtfeuern hier nicht verfügbar. Der Mod muss auch auf dem Server installiert sein.",
 "Die Reise zum Rückkehrleuchtfeuer konnte nicht abgeschlossen werden.",
 "Boss besiegt. Deine Rückkehrleuchtfeuer in diesem Dungeon wurden zurückgesetzt.",
 "Wird geladen … %d %%"},
// es
{"Hollow Halls Progression",
 "Baliza de regreso",
 "Baliza de regreso %u de %u alcanzada. Haz clic en esta mazmorra en el mapa del mundo para volver aquí más tarde.",
 "Viajando a la baliza de regreso %u...",
 "Todavía no has alcanzado esa baliza de regreso.",
 "Ya hay un viaje a una baliza en curso.",
 "Comprobando si este servidor usa Hollow Halls Progression. Vuelve a intentarlo en unos segundos.",
 "Este servidor no usa Hollow Halls Progression, así que el viaje a las balizas no está disponible aquí. El servidor también necesita el mod.",
 "No se pudo completar el viaje a la baliza de regreso.",
 "Jefe derrotado. Tus balizas de regreso de esta mazmorra se han reiniciado.",
 "Cargando… %d %%"},
// ja
{"Hollow Halls Progression",
 "帰還のビーコン",
 "帰還のビーコン %u/%u に到達しました。後でここに戻るには、ワールドマップでこのダンジョンをクリックしてください。",
 "帰還のビーコン %u へ移動中...",
 "その帰還のビーコンにはまだ到達していません。",
 "ビーコンへの移動はすでに進行中です。",
 "このサーバーで Hollow Halls Progression が動作しているか確認中です。数秒後にもう一度お試しください。",
 "このサーバーでは Hollow Halls Progression が動作していないため、ここではビーコンへの移動は使えません。サーバーにもMODのインストールが必要です。",
 "帰還のビーコンへの移動を完了できませんでした。",
 "ボスを倒しました。このダンジョンの帰還のビーコンはリセットされました。",
 "読み込み中… %d%%"},
// ko
{"Hollow Halls Progression",
 "귀환 신호기",
 "귀환 신호기 %u/%u에 도달했습니다. 나중에 이곳으로 돌아오려면 월드 맵에서 이 던전을 클릭하세요.",
 "귀환 신호기 %u(으)로 이동하는 중...",
 "아직 그 귀환 신호기에 도달하지 않았습니다.",
 "이미 신호기로 이동하는 중입니다.",
 "이 서버에서 Hollow Halls Progression을 사용하는지 확인하는 중입니다. 몇 초 후에 다시 시도하세요.",
 "이 서버는 Hollow Halls Progression을 사용하지 않아 여기서는 신호기 이동을 할 수 없습니다. 서버에도 모드를 설치해야 합니다.",
 "귀환 신호기로 이동하지 못했습니다.",
 "보스를 처치했습니다. 이 던전의 귀환 신호기가 초기화되었습니다.",
 "불러오는 중… %d%%"},
// pl
{"Hollow Halls Progression",
 "Latarnia powrotu",
 "Osiągnięto latarnię powrotu %u z %u. Kliknij ten loch na mapie świata, aby później tu wrócić.",
 "Podróż do latarni powrotu %u...",
 "Nie dotarłeś jeszcze do tej latarni powrotu.",
 "Podróż do latarni już trwa.",
 "Trwa sprawdzanie, czy ten serwer używa Hollow Halls Progression. Spróbuj ponownie za kilka sekund.",
 "Ten serwer nie używa Hollow Halls Progression, więc podróż do latarni jest tu niedostępna. Mod musi być zainstalowany także na serwerze.",
 "Nie udało się ukończyć podróży do latarni powrotu.",
 "Boss pokonany. Twoje latarnie powrotu w tym lochu zostały zresetowane.",
 "Wczytywanie… %d%%"},
// pt (Brazil)
{"Hollow Halls Progression",
 "Farol de Retorno",
 "Farol de Retorno %u de %u alcançado. Clique nesta masmorra no mapa-múndi para voltar aqui depois.",
 "Viajando para o Farol de Retorno %u...",
 "Você ainda não alcançou esse Farol de Retorno.",
 "Já há uma viagem para um farol em andamento.",
 "Verificando se este servidor usa o Hollow Halls Progression. Tente novamente em alguns segundos.",
 "Este servidor não usa o Hollow Halls Progression, então a viagem aos faróis não está disponível aqui. O servidor também precisa do mod instalado.",
 "Não foi possível concluir a viagem ao Farol de Retorno.",
 "Chefe derrotado. Seus Faróis de Retorno nesta masmorra foram redefinidos.",
 "Carregando… %d%%"},
// ru
{"Hollow Halls Progression",
 "Маяк возвращения",
 "Достигнут маяк возвращения %u из %u. Нажмите на это подземелье на карте мира, чтобы позже вернуться сюда.",
 "Перемещение к маяку возвращения %u...",
 "Вы ещё не добрались до этого маяка возвращения.",
 "Перемещение к маяку уже выполняется.",
 "Проверяем, использует ли этот сервер Hollow Halls Progression. Повторите попытку через несколько секунд.",
 "Этот сервер не использует Hollow Halls Progression, поэтому перемещение к маякам здесь недоступно. Мод должен быть установлен и на сервере.",
 "Не удалось завершить перемещение к маяку возвращения.",
 "Босс побеждён. Ваши маяки возвращения в этом подземелье сброшены.",
 "Загрузка… %d%%"},
// zh-hans
{"Hollow Halls Progression",
 "回归信标",
 "已到达回归信标 %u/%u。之后可在世界地图上点击此地下城返回这里。",
 "正在前往回归信标 %u……",
 "你还没有到达那个回归信标。",
 "已有一次信标传送正在进行。",
 "正在确认此服务器是否运行 Hollow Halls Progression，请几秒后再试。",
 "此服务器未运行 Hollow Halls Progression，因此这里无法使用信标传送。服务器也需要安装此模组。",
 "无法完成前往回归信标的传送。",
 "已击败首领。你在此地下城的回归信标已重置。",
 "加载中… %d%%"},
// zh-hant
{"Hollow Halls Progression",
 "回歸信標",
 "已抵達回歸信標 %u/%u。之後可在世界地圖上點擊此地城返回這裡。",
 "正在前往回歸信標 %u……",
 "你還沒有抵達那個回歸信標。",
 "已有一次信標傳送正在進行。",
 "正在確認此伺服器是否執行 Hollow Halls Progression，請幾秒後再試。",
 "此伺服器未執行 Hollow Halls Progression，因此這裡無法使用信標傳送。伺服器也需要安裝此模組。",
 "無法完成前往回歸信標的傳送。",
 "已擊敗首領。你在此地城的回歸信標已重置。",
 "載入中… %d%%"},
// tr
{"Hollow Halls Progression",
 "Dönüş İşareti",
 "Dönüş İşareti %u / %u bulundu. Daha sonra buraya dönmek için dünya haritasında bu zindana tıkla.",
 "Dönüş İşareti %u konumuna gidiliyor...",
 "Bu Dönüş İşaretine henüz ulaşmadın.",
 "Bir işarete yolculuk zaten sürüyor.",
 "Bu sunucunun Hollow Halls Progression kullanıp kullanmadığı kontrol ediliyor. Lütfen birkaç saniye sonra tekrar dene.",
 "Bu sunucu Hollow Halls Progression kullanmıyor, bu yüzden burada işaretlere yolculuk yapılamaz. Modun sunucuya da kurulması gerekir.",
 "Dönüş İşaretine yolculuk tamamlanamadı.",
 "Boss yenildi. Bu zindandaki Dönüş İşaretlerin sıfırlandı.",
 "Yükleniyor… %%%d"},
// uk
{"Hollow Halls Progression",
 "Маяк повернення",
 "Досягнуто маяк повернення %u з %u. Натисніть на це підземелля на мапі світу, щоб згодом повернутися сюди.",
 "Переміщення до маяка повернення %u...",
 "Ви ще не дісталися цього маяка повернення.",
 "Переміщення до маяка вже триває.",
 "Перевіряємо, чи використовує цей сервер Hollow Halls Progression. Спробуйте ще раз за кілька секунд.",
 "Цей сервер не використовує Hollow Halls Progression, тому переміщення до маяків тут недоступне. Мод має бути встановлений і на сервері.",
 "Не вдалося завершити переміщення до маяка повернення.",
 "Боса переможено. Ваші маяки повернення в цьому підземеллі скинуто.",
 "Завантаження… %d%%"},
// th
{"Hollow Halls Progression",
 "บีคอนกลับคืน",
 "ไปถึงบีคอนกลับคืน %u จาก %u แล้ว คลิกดันเจี้ยนนี้บนแผนที่โลกเพื่อกลับมาที่นี่ภายหลัง",
 "กำลังเดินทางไปยังบีคอนกลับคืน %u...",
 "คุณยังไปไม่ถึงบีคอนกลับคืนนั้น",
 "มีการเดินทางไปยังบีคอนอยู่แล้ว",
 "กำลังตรวจสอบว่าเซิร์ฟเวอร์นี้ใช้ Hollow Halls Progression หรือไม่ โปรดลองอีกครั้งในอีกไม่กี่วินาที",
 "เซิร์ฟเวอร์นี้ไม่ได้ใช้ Hollow Halls Progression จึงไม่สามารถเดินทางไปยังบีคอนได้ที่นี่ ต้องติดตั้งม็อดบนเซิร์ฟเวอร์ด้วย",
 "ไม่สามารถเดินทางไปยังบีคอนกลับคืนได้สำเร็จ",
 "ปราบบอสแล้ว บีคอนกลับคืนของคุณในดันเจี้ยนนี้ถูกรีเซ็ตแล้ว",
 "กำลังโหลด… %d%%"},
};

inline const char* Get(Lang lang, Id id) {
    auto l = size_t(lang) < size_t(Lang::count) ? size_t(lang) : 0;
    return kTexts[l][id];
}

// "auto" (or anything unknown) is -1; otherwise the language with that code ("de", "zh-hans").
inline int ParseCode(std::string_view code) {
    std::string lower;
    for (char c : code) if (c != ' ' && c != '\t') lower += char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    if (lower == "zh" || lower == "zh-cn" || lower == "zh_cn" || lower == "zh_hans") lower = "zh-hans";
    if (lower == "zh-tw" || lower == "zh_tw" || lower == "zh_hant") lower = "zh-hant";
    if (lower == "pt-br" || lower == "pt_br") lower = "pt";
    for (size_t i = 0; i < size_t(Lang::count); ++i) if (lower == kCodes[i]) return int(i);
    return -1;
}

// The game's language from its own word for "Legendary" (the rarity), as Find tells it.
// False when the text is empty or unknown; the caller keeps its previous language then.
inline bool Detect(std::string_view legendary, Lang& out) {
    auto has = [&](const char* part) { return legendary.find(part) != std::string_view::npos; };
    auto starts = [&](const char* prefix) {
        size_t n = strlen(prefix);
        if (legendary.size() < n) return false;
        for (size_t i = 0; i < n; ++i) {
            char a = legendary[i];
            if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
            if (a != prefix[i]) return false;
        }
        return true;
    };
    if (legendary.empty()) return false;
    if (starts("legendary")) out = Lang::en;
    else if (starts("legendär") || starts("legendÄr")) out = Lang::de;
    else if (starts("légend") || starts("lÉgend")) out = Lang::fr;
    else if (starts("legendari")) out = Lang::es;
    else if (starts("leggend")) out = Lang::it;
    else if (starts("lend")) out = Lang::pt;
    else if (starts("legendarn")) out = Lang::pl;
    else if (starts("efsane")) out = Lang::tr;
    else if (has("Леген") || has("ЛЕГЕН") || has("леген")) {
        // Russian or Ukrainian: the letters only Ukrainian has, else its endings (-ний, -на, -не);
        // the rest is Russian, whichever form the game uses (-ный, -ая, -ое, ...).
        auto ends = [&](const char* suffix) {
            size_t n = strlen(suffix);
            return legendary.size() >= n && legendary.substr(legendary.size() - n) == suffix;
        };
        bool uk = has("і") || has("І") || has("ї") || has("Ї") || has("є") || has("Є") || has("ґ") || has("Ґ") ||
                  ends("ний") || ends("на") || ends("не") || ends("НИЙ") || ends("НА") || ends("НЕ");
        out = uk && !has("ы") && !has("Ы") ? Lang::uk : Lang::ru;
    }
    else if (has("전설")) out = Lang::ko;
    else if (has("レジェ") || has("伝説")) out = Lang::ja;
    else if (has("传") || has("说")) out = Lang::zhHans;
    else if (has("傳") || has("說")) out = Lang::zhHant;
    else if (legendary.size() >= 3 && uint8_t(legendary[0]) == 0xE0 && (uint8_t(legendary[1]) == 0xB8 || uint8_t(legendary[1]) == 0xB9)) out = Lang::th;
    else return false;
    return true;
}

}  // namespace hhp::text
