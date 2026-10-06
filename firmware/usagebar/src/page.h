#pragma once
#include <pgmspace.h>

// GET / : status + settings page. Language, brightness and the weather place are saved through
// POST /api/settings; when an admin password is set, a password-only box logs in (POST /api/login,
// session cookie). Place search uses Open-Meteo's geocoding API from the browser.
static const char ROOT_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SmallTV</title>
<style>
:root{--bg:#fff;--fg:#1a1a1a;--mut:#666;--card:#f4f4f5;--acc:#2563eb;--bd:#ddd}
@media (prefers-color-scheme:dark){:root{--bg:#111;--fg:#eee;--mut:#999;--card:#1c1c1f;--acc:#60a5fa;--bd:#333}}
body{background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif;max-width:560px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:0 0 12px}h2{font-size:15px;margin:0 0 8px}
section{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:12px;margin:12px 0}
table{width:100%;border-collapse:collapse}td,th{text-align:left;padding:4px 6px;border-bottom:1px solid var(--bd)}
button,select,input{font:inherit;border-radius:6px;border:1px solid var(--bd);padding:6px 10px;background:var(--bg);color:var(--fg)}
button{cursor:pointer}button.on{background:var(--acc);color:#fff;border-color:var(--acc)}
#results button,#nets button{display:block;width:100%;text-align:left;margin:4px 0}
.m{color:var(--mut);font-size:13px}a{color:var(--acc);margin-right:12px}
</style></head><body>
<h1 id="t_title"></h1>
<section id="loginbox" hidden><h2 id="a_login"></h2>
<input type="password" id="lp" style="width:60%"> <button id="a_login_btn" onclick="login()"></button><p class="m" id="lmsg"></p></section>
<section id="wifisec"><h2 id="w_title"></h2><p id="w_cur"></p>
<button id="w_scan" onclick="scan()"></button><div id="nets"></div>
<div id="wform" hidden><p id="w_ssid"></p><input type="password" id="wp" style="width:60%"> <button id="w_conn" onclick="joinWifi()"></button></div>
<p class="m" id="wmsg"></p></section>
<section id="tokensec" hidden><h2 id="w_tok"></h2><code id="token"></code><p class="m" id="w_tokhint"></p></section>
<section id="usagesec"><h2 id="t_usage"></h2><table id="usage"></table><p class="m" id="age"></p></section>
<section><h2 id="t_lang"></h2>
<select id="lang"><option value="ko">한국어</option><option value="en">English</option><option value="ja">日本語</option><option value="zh">简体中文</option><option value="zh-TW">繁體中文</option>
<option value="es">Español</option><option value="pt">Português</option><option value="fr">Français</option><option value="de">Deutsch</option><option value="it">Italiano</option>
<option value="ru">Русский</option><option value="uk">Українська</option><option value="pl">Polski</option><option value="nl">Nederlands</option>
<option value="tr">Türkçe</option><option value="vi">Tiếng Việt</option><option value="id">Bahasa Indonesia</option><option value="th">ไทย</option><option value="ar">العربية</option></select>
</section>
<section><h2 id="t_place"></h2><p id="city"></p>
<input id="q" style="width:60%"> <button id="search"></button><div id="results"></div></section>
<section><h2 id="t_bl"></h2><div id="bl"></div></section>
<section><h2 id="a_title"></h2><p id="a_state"></p>
<input type="password" id="np" style="width:50%"> <button id="a_set" onclick="setPass()"></button> <button id="a_off" onclick="save({pass:''})"></button></section>
<p class="m" id="msg"></p>
<p class="m"><a href="/update" id="t_update"></a><a href="/api/info">info</a><a href="/api/log">log</a></p>
<script>
const T={
ko:{title:"AI 사용량 표시기",usage:"사용량",lang:"언어",place:"날씨 지역",bl:"밝기",search:"검색",svc:"서비스",win:"창",left:"남음",reset:"리셋까지",
 ago:s=>`마지막 수신: ${s}초 전`,none:"아직 받은 데이터가 없습니다.",saved:"저장했습니다.",auth:"관리자 인증이 필요합니다.",nores:"검색 결과가 없습니다.",update:"펌웨어 업데이트",d:"일",h:"시간",m:"분",sp:" "},
en:{title:"AI usage display",usage:"Usage",lang:"Language",place:"Weather location",bl:"Brightness",search:"Search",svc:"Service",win:"Window",left:"Left",reset:"Resets in",
 ago:s=>`Last update: ${s}s ago`,none:"No data received yet.",saved:"Saved.",auth:"Admin login required.",nores:"No results.",update:"Firmware update",d:"d",h:"h",m:"m",sp:" "},
ja:{title:"AI 使用量ディスプレイ",usage:"使用量",lang:"言語",place:"天気の地域",bl:"明るさ",search:"検索",svc:"サービス",win:"期間",left:"残り",reset:"リセットまで",
 ago:s=>`最終受信: ${s}秒前`,none:"まだデータがありません。",saved:"保存しました。",auth:"管理者認証が必要です。",nores:"見つかりませんでした。",update:"ファームウェア更新",d:"日",h:"時間",m:"分",sp:""},
zh:{title:"AI 用量显示器",usage:"用量",lang:"语言",place:"天气地区",bl:"亮度",search:"搜索",svc:"服务",win:"周期",left:"剩余",reset:"重置倒计时",
 ago:s=>`上次接收: ${s}秒前`,none:"尚未收到数据。",saved:"已保存。",auth:"需要管理员认证。",nores:"没有结果。",update:"固件更新",d:"天",h:"小时",m:"分",sp:""},
es:{title:"Monitor de uso de IA",usage:"Uso",lang:"Idioma",place:"Ubicación del clima",bl:"Brillo",search:"Buscar",svc:"Servicio",win:"Periodo",left:"Restante",reset:"Se reinicia en",
 ago:s=>`Última actualización: hace ${s} s`,none:"Aún no hay datos.",saved:"Guardado.",auth:"Se requiere acceso de administrador.",nores:"Sin resultados.",update:"Actualizar firmware",d:"d",h:"h",m:"min",sp:" "},
pt:{title:"Monitor de uso de IA",usage:"Uso",lang:"Idioma",place:"Local do clima",bl:"Brilho",search:"Pesquisar",svc:"Serviço",win:"Período",left:"Restante",reset:"Reinicia em",
 ago:s=>`Última atualização: há ${s} s`,none:"Ainda sem dados.",saved:"Salvo.",auth:"É necessário login de administrador.",nores:"Nenhum resultado.",update:"Atualizar firmware",d:"d",h:"h",m:"min",sp:" "},
fr:{title:"Moniteur d'utilisation IA",usage:"Utilisation",lang:"Langue",place:"Lieu de la météo",bl:"Luminosité",search:"Rechercher",svc:"Service",win:"Période",left:"Restant",reset:"Réinitialisation dans",
 ago:s=>`Dernière mise à jour : il y a ${s} s`,none:"Aucune donnée pour l'instant.",saved:"Enregistré.",auth:"Connexion administrateur requise.",nores:"Aucun résultat.",update:"Mise à jour du firmware",d:"j",h:"h",m:"min",sp:" "},
de:{title:"KI-Nutzungsanzeige",usage:"Nutzung",lang:"Sprache",place:"Wetterort",bl:"Helligkeit",search:"Suchen",svc:"Dienst",win:"Zeitraum",left:"Übrig",reset:"Zurückgesetzt in",
 ago:s=>`Letzte Aktualisierung: vor ${s} s`,none:"Noch keine Daten.",saved:"Gespeichert.",auth:"Admin-Anmeldung erforderlich.",nores:"Keine Ergebnisse.",update:"Firmware-Update",d:"T",h:"h",m:"min",sp:" "},
it:{title:"Monitor utilizzo IA",usage:"Utilizzo",lang:"Lingua",place:"Località meteo",bl:"Luminosità",search:"Cerca",svc:"Servizio",win:"Periodo",left:"Rimanente",reset:"Si azzera tra",
 ago:s=>`Ultimo aggiornamento: ${s} s fa`,none:"Ancora nessun dato.",saved:"Salvato.",auth:"Accesso amministratore richiesto.",nores:"Nessun risultato.",update:"Aggiornamento firmware",d:"g",h:"h",m:"min",sp:" "},
"zh-TW":{title:"AI 用量顯示器",usage:"用量",lang:"語言",place:"天氣地區",bl:"亮度",search:"搜尋",svc:"服務",win:"週期",left:"剩餘",reset:"重置倒數",
 ago:s=>`上次接收: ${s}秒前`,none:"尚未收到資料。",saved:"已儲存。",auth:"需要管理員驗證。",nores:"沒有結果。",update:"韌體更新",d:"天",h:"小時",m:"分",sp:""},
ru:{title:"Монитор использования ИИ",usage:"Использование",lang:"Язык",place:"Место для погоды",bl:"Яркость",search:"Найти",svc:"Сервис",win:"Период",left:"Осталось",reset:"Сброс через",
 ago:s=>`Последнее обновление: ${s} с назад`,none:"Данных пока нет.",saved:"Сохранено.",auth:"Нужен вход администратора.",nores:"Ничего не найдено.",update:"Обновление прошивки",d:"д",h:"ч",m:"мин",sp:" "},
uk:{title:"Монітор використання ШІ",usage:"Використання",lang:"Мова",place:"Місце для погоди",bl:"Яскравість",search:"Знайти",svc:"Сервіс",win:"Період",left:"Залишилось",reset:"Скидання через",
 ago:s=>`Останнє оновлення: ${s} с тому`,none:"Даних ще немає.",saved:"Збережено.",auth:"Потрібен вхід адміністратора.",nores:"Нічого не знайдено.",update:"Оновлення прошивки",d:"д",h:"год",m:"хв",sp:" "},
pl:{title:"Monitor użycia AI",usage:"Użycie",lang:"Język",place:"Miejsce pogody",bl:"Jasność",search:"Szukaj",svc:"Usługa",win:"Okres",left:"Pozostało",reset:"Reset za",
 ago:s=>`Ostatnia aktualizacja: ${s} s temu`,none:"Brak danych.",saved:"Zapisano.",auth:"Wymagane logowanie administratora.",nores:"Brak wyników.",update:"Aktualizacja oprogramowania",d:"d",h:"godz",m:"min",sp:" "},
nl:{title:"AI-gebruiksmonitor",usage:"Gebruik",lang:"Taal",place:"Weerlocatie",bl:"Helderheid",search:"Zoeken",svc:"Dienst",win:"Periode",left:"Over",reset:"Reset over",
 ago:s=>`Laatste update: ${s} s geleden`,none:"Nog geen gegevens.",saved:"Opgeslagen.",auth:"Beheerderslogin vereist.",nores:"Geen resultaten.",update:"Firmware-update",d:"d",h:"u",m:"min",sp:" "},
tr:{title:"Yapay zekâ kullanım ekranı",usage:"Kullanım",lang:"Dil",place:"Hava durumu konumu",bl:"Parlaklık",search:"Ara",svc:"Hizmet",win:"Dönem",left:"Kalan",reset:"Sıfırlanmaya",
 ago:s=>`Son güncelleme: ${s} sn önce`,none:"Henüz veri yok.",saved:"Kaydedildi.",auth:"Yönetici girişi gerekli.",nores:"Sonuç yok.",update:"Yazılım güncelleme",d:"g",h:"sa",m:"dk",sp:" "},
vi:{title:"Màn hình mức dùng AI",usage:"Mức dùng",lang:"Ngôn ngữ",place:"Vị trí thời tiết",bl:"Độ sáng",search:"Tìm",svc:"Dịch vụ",win:"Chu kỳ",left:"Còn lại",reset:"Đặt lại sau",
 ago:s=>`Cập nhật lần cuối: ${s} giây trước`,none:"Chưa có dữ liệu.",saved:"Đã lưu.",auth:"Cần đăng nhập quản trị.",nores:"Không có kết quả.",update:"Cập nhật firmware",d:"ng",h:"h",m:"p",sp:" "},
id:{title:"Monitor penggunaan AI",usage:"Penggunaan",lang:"Bahasa",place:"Lokasi cuaca",bl:"Kecerahan",search:"Cari",svc:"Layanan",win:"Periode",left:"Sisa",reset:"Reset dalam",
 ago:s=>`Pembaruan terakhir: ${s} dtk lalu`,none:"Belum ada data.",saved:"Tersimpan.",auth:"Perlu login admin.",nores:"Tidak ada hasil.",update:"Pembaruan firmware",d:"h",h:"j",m:"m",sp:" "},
th:{title:"จอแสดงการใช้งาน AI",usage:"การใช้งาน",lang:"ภาษา",place:"ตำแหน่งสภาพอากาศ",bl:"ความสว่าง",search:"ค้นหา",svc:"บริการ",win:"รอบ",left:"คงเหลือ",reset:"รีเซ็ตใน",
 ago:s=>`อัปเดตล่าสุด: ${s} วินาทีที่แล้ว`,none:"ยังไม่มีข้อมูล",saved:"บันทึกแล้ว",auth:"ต้องเข้าสู่ระบบผู้ดูแล",nores:"ไม่พบผลลัพธ์",update:"อัปเดตเฟิร์มแวร์",d:"ว.",h:"ชม.",m:"น.",sp:" "},
ar:{title:"شاشة استخدام الذكاء الاصطناعي",usage:"الاستخدام",lang:"اللغة",place:"موقع الطقس",bl:"السطوع",search:"بحث",svc:"الخدمة",win:"الفترة",left:"المتبقي",reset:"إعادة الضبط بعد",
 ago:s=>`آخر تحديث: قبل ${s} ث`,none:"لا توجد بيانات بعد.",saved:"تم الحفظ.",auth:"يلزم تسجيل دخول المسؤول.",nores:"لا توجد نتائج.",update:"تحديث البرنامج الثابت",d:"ي",h:"س",m:"د",sp:" "}};
const A={ko:["관리자 암호","켜짐","꺼짐","암호 설정","끄기","로그인","암호가 틀렸습니다.","암호"],
en:["Admin password","On","Off","Set password","Turn off","Log in","Wrong password.","Password"],
ja:["管理者パスワード","オン","オフ","パスワード設定","オフにする","ログイン","パスワードが違います。","パスワード"],
zh:["管理员密码","已开启","已关闭","设置密码","关闭","登录","密码错误。","密码"],
es:["Contraseña de administrador","Activada","Desactivada","Establecer","Desactivar","Iniciar sesión","Contraseña incorrecta.","Contraseña"],
pt:["Senha de administrador","Ativada","Desativada","Definir senha","Desativar","Entrar","Senha incorreta.","Senha"],
fr:["Mot de passe administrateur","Activé","Désactivé","Définir","Désactiver","Se connecter","Mot de passe incorrect.","Mot de passe"],
de:["Admin-Passwort","An","Aus","Passwort setzen","Ausschalten","Anmelden","Falsches Passwort.","Passwort"],
it:["Password amministratore","Attiva","Disattiva","Imposta password","Disattiva","Accedi","Password errata.","Password"],
"zh-TW":["管理員密碼","已開啟","已關閉","設定密碼","關閉","登入","密碼錯誤。","密碼"],
ru:["Пароль администратора","Вкл.","Выкл.","Задать пароль","Выключить","Войти","Неверный пароль.","Пароль"],
uk:["Пароль адміністратора","Увімк.","Вимк.","Встановити пароль","Вимкнути","Увійти","Невірний пароль.","Пароль"],
pl:["Hasło administratora","Włączone","Wyłączone","Ustaw hasło","Wyłącz","Zaloguj","Złe hasło.","Hasło"],
nl:["Beheerderswachtwoord","Aan","Uit","Wachtwoord instellen","Uitzetten","Inloggen","Onjuist wachtwoord.","Wachtwoord"],
tr:["Yönetici şifresi","Açık","Kapalı","Şifre belirle","Kapat","Giriş","Yanlış şifre.","Şifre"],
vi:["Mật khẩu quản trị","Bật","Tắt","Đặt mật khẩu","Tắt","Đăng nhập","Sai mật khẩu.","Mật khẩu"],
id:["Kata sandi admin","Aktif","Nonaktif","Atur kata sandi","Matikan","Masuk","Kata sandi salah.","Kata sandi"],
th:["รหัสผ่านผู้ดูแล","เปิด","ปิด","ตั้งรหัสผ่าน","ปิด","เข้าสู่ระบบ","รหัสผ่านไม่ถูกต้อง","รหัสผ่าน"],
ar:["كلمة مرور المسؤول","مفعّلة","معطّلة","تعيين كلمة المرور","إيقاف","تسجيل الدخول","كلمة المرور خاطئة.","كلمة المرور"]};
// Wi-Fi strings: [title, not set, scan, connect, password, saved+rebooting, push token, token hint, setup hint]
const W={ko:["Wi-Fi","설정 안 됨","주변 Wi-Fi 찾기","연결","Wi-Fi 비밀번호","저장했습니다. 기기가 재시작해서 이 Wi-Fi에 연결합니다. 휴대폰도 같은 Wi-Fi로 옮긴 뒤 기기 화면에 나오는 주소로 접속하세요.","전송 토큰","Mac에서: tools/install_launchd.sh <기기 IP> <토큰>","먼저 아래에서 집 Wi-Fi를 연결하세요."],
en:["Wi-Fi","not set","Find networks","Connect","Wi-Fi password","Saved. The device restarts and joins this network. Move your phone to the same Wi-Fi and open the address shown on the display.","Push token","On the Mac: tools/install_launchd.sh <device IP> <token>","First connect the device to your Wi-Fi below."],
ja:["Wi-Fi","未設定","Wi-Fiを検索","接続","Wi-Fiパスワード","保存しました。端末が再起動してこのWi-Fiに接続します。スマホも同じWi-Fiに切り替えて、画面に表示されるアドレスを開いてください。","送信トークン","Macで: tools/install_launchd.sh <端末のIP> <トークン>","まず下で自宅のWi-Fiに接続してください。"],
zh:["Wi-Fi","未设置","搜索 Wi-Fi","连接","Wi-Fi 密码","已保存。设备将重启并连接此 Wi-Fi。请将手机切换到同一 Wi-Fi，然后打开屏幕上显示的地址。","推送令牌","在 Mac 上: tools/install_launchd.sh <设备 IP> <令牌>","请先在下方连接 Wi-Fi。"],
es:["Wi-Fi","sin configurar","Buscar redes","Conectar","Contraseña Wi-Fi","Guardado. El dispositivo se reinicia y se une a esta red. Cambia el teléfono a la misma Wi-Fi y abre la dirección que muestra la pantalla.","Token de envío","En el Mac: tools/install_launchd.sh <IP del dispositivo> <token>","Primero conecta el dispositivo a tu Wi-Fi aquí abajo."],
pt:["Wi-Fi","não configurado","Procurar redes","Conectar","Senha do Wi-Fi","Salvo. O dispositivo reinicia e entra nesta rede. Mude o celular para o mesmo Wi-Fi e abra o endereço mostrado na tela.","Token de envio","No Mac: tools/install_launchd.sh <IP do dispositivo> <token>","Primeiro conecte o dispositivo ao seu Wi-Fi abaixo."],
fr:["Wi-Fi","non configuré","Chercher les réseaux","Connecter","Mot de passe Wi-Fi","Enregistré. L'appareil redémarre et rejoint ce réseau. Passez votre téléphone sur le même Wi-Fi et ouvrez l'adresse affichée à l'écran.","Jeton d'envoi","Sur le Mac : tools/install_launchd.sh <IP de l'appareil> <jeton>","Connectez d'abord l'appareil à votre Wi-Fi ci-dessous."],
de:["WLAN","nicht eingerichtet","Netzwerke suchen","Verbinden","WLAN-Passwort","Gespeichert. Das Gerät startet neu und verbindet sich mit diesem Netz. Wechseln Sie mit dem Handy ins selbe WLAN und öffnen Sie die angezeigte Adresse.","Push-Token","Auf dem Mac: tools/install_launchd.sh <Geräte-IP> <Token>","Verbinden Sie das Gerät zuerst unten mit Ihrem WLAN."],
it:["Wi-Fi","non configurato","Cerca reti","Connetti","Password Wi-Fi","Salvato. Il dispositivo si riavvia e si collega a questa rete. Passa il telefono alla stessa Wi-Fi e apri l'indirizzo mostrato sullo schermo.","Token di invio","Sul Mac: tools/install_launchd.sh <IP del dispositivo> <token>","Prima collega il dispositivo alla tua Wi-Fi qui sotto."],
"zh-TW":["Wi-Fi","未設定","搜尋 Wi-Fi","連線","Wi-Fi 密碼","已儲存。裝置將重新啟動並連上此 Wi-Fi。請將手機切換到同一個 Wi-Fi，再開啟螢幕上顯示的位址。","推送權杖","在 Mac 上: tools/install_launchd.sh <裝置 IP> <權杖>","請先在下方連上 Wi-Fi。"],
ru:["Wi-Fi","не настроено","Найти сети","Подключить","Пароль Wi-Fi","Сохранено. Устройство перезапустится и подключится к этой сети. Переключите телефон на ту же Wi-Fi и откройте адрес с экрана.","Токен отправки","На Mac: tools/install_launchd.sh <IP устройства> <токен>","Сначала подключите устройство к Wi-Fi ниже."],
uk:["Wi-Fi","не налаштовано","Знайти мережі","Підключити","Пароль Wi-Fi","Збережено. Пристрій перезапуститься і підключиться до цієї мережі. Переключіть телефон на ту саму Wi-Fi і відкрийте адресу з екрана.","Токен надсилання","На Mac: tools/install_launchd.sh <IP пристрою> <токен>","Спочатку підключіть пристрій до Wi-Fi нижче."],
pl:["Wi-Fi","nie ustawiono","Szukaj sieci","Połącz","Hasło Wi-Fi","Zapisano. Urządzenie uruchomi się ponownie i połączy z tą siecią. Przełącz telefon na tę samą Wi-Fi i otwórz adres z ekranu.","Token wysyłania","Na Macu: tools/install_launchd.sh <IP urządzenia> <token>","Najpierw połącz urządzenie z Wi-Fi poniżej."],
nl:["Wifi","niet ingesteld","Netwerken zoeken","Verbinden","Wifi-wachtwoord","Opgeslagen. Het apparaat herstart en maakt verbinding met dit netwerk. Zet je telefoon op dezelfde wifi en open het adres op het scherm.","Push-token","Op de Mac: tools/install_launchd.sh <IP van apparaat> <token>","Verbind het apparaat eerst hieronder met je wifi."],
tr:["Wi-Fi","ayarlanmadı","Ağları ara","Bağlan","Wi-Fi şifresi","Kaydedildi. Cihaz yeniden başlayıp bu ağa bağlanacak. Telefonunuzu aynı Wi-Fi'a geçirip ekranda görünen adresi açın.","Gönderim anahtarı","Mac'te: tools/install_launchd.sh <cihaz IP> <anahtar>","Önce cihazı aşağıdan Wi-Fi'ınıza bağlayın."],
vi:["Wi-Fi","chưa thiết lập","Tìm mạng","Kết nối","Mật khẩu Wi-Fi","Đã lưu. Thiết bị sẽ khởi động lại và kết nối mạng này. Chuyển điện thoại sang cùng Wi-Fi rồi mở địa chỉ hiện trên màn hình.","Mã gửi","Trên Mac: tools/install_launchd.sh <IP thiết bị> <mã>","Trước tiên hãy kết nối thiết bị với Wi-Fi bên dưới."],
id:["Wi-Fi","belum diatur","Cari jaringan","Hubungkan","Kata sandi Wi-Fi","Tersimpan. Perangkat akan mulai ulang dan bergabung ke jaringan ini. Pindahkan ponsel ke Wi-Fi yang sama lalu buka alamat di layar.","Token kirim","Di Mac: tools/install_launchd.sh <IP perangkat> <token>","Hubungkan perangkat ke Wi-Fi Anda di bawah terlebih dahulu."],
th:["Wi-Fi","ยังไม่ได้ตั้งค่า","ค้นหาเครือข่าย","เชื่อมต่อ","รหัสผ่าน Wi-Fi","บันทึกแล้ว อุปกรณ์จะรีสตาร์ทและเชื่อมต่อเครือข่ายนี้ ให้เปลี่ยนโทรศัพท์ไปใช้ Wi-Fi เดียวกันแล้วเปิดที่อยู่ที่แสดงบนจอ","โทเค็นส่งข้อมูล","บน Mac: tools/install_launchd.sh <IP อุปกรณ์> <โทเค็น>","เชื่อมต่ออุปกรณ์กับ Wi-Fi ด้านล่างก่อน"],
ar:["واي فاي","غير مُعدّ","البحث عن الشبكات","اتصال","كلمة مرور الواي فاي","تم الحفظ. سيعيد الجهاز التشغيل ويتصل بهذه الشبكة. انقل هاتفك إلى نفس الشبكة وافتح العنوان الظاهر على الشاشة.","رمز الإرسال","على الماك: tools/install_launchd.sh <عنوان IP للجهاز> <الرمز>","صِل الجهاز بشبكة الواي فاي أدناه أولاً."]};
const $=id=>document.getElementById(id);
let S={},L=T.ko;
function dur(s){if(s<=0)return"0"+L.m;const m=Math.ceil(s/60),d=Math.floor(m/1440),h=Math.floor(m%1440/60),mm=m%60;
 return d?d+L.d+(h?L.sp+h+L.h:""):h?h+L.h+(mm?L.sp+mm+L.m:""):mm+L.m}
function texts(){for(const k of["title","usage","lang","place","bl","search","update"]){const e=$("t_"+k)||$(k);if(e)e.textContent=L[k]}
 $("city").textContent=S.city?`${S.city} (${(+S.lat).toFixed(2)}, ${(+S.lon).toFixed(2)})`:"";
 $("bl").innerHTML=[10,30,60,100].map(v=>`<button class="${v==S.bl?"on":""}" onclick="save({bl:${v}})">${v}%</button>`).join(" ");
 const a=A[S.lang]||A.en;$("a_title").textContent=a[0];$("a_state").textContent=S.locked?a[1]:a[2];$("a_set").textContent=a[3];
 $("a_off").textContent=a[4];$("a_off").hidden=!S.locked;$("a_login").textContent=a[5];$("a_login_btn").textContent=a[5];
 $("np").placeholder=$("lp").placeholder=a[7];
 const w=W[S.lang]||W.en;$("w_title").textContent=w[0];$("w_cur").textContent=(S.setup?w[8]+" ":"")+(S.wifi||w[1]);$("w_scan").textContent=w[2];
 $("w_conn").textContent=w[3];$("wp").placeholder=w[4];$("w_tok").textContent=w[6];$("w_tokhint").textContent=w[7];
 $("tokensec").hidden=!S.token;$("token").textContent=S.token||"";$("usagesec").hidden=!!S.setup;$("loginbox").hidden=!(S.locked&&!S.authed)&&!location.search.includes("login")}
async function usage(){try{const u=await(await fetch("/api/usage")).json();
 if(!u.have){$("usage").innerHTML="";$("age").textContent=L.none;return}
 let h=`<tr><th>${L.svc}</th><th>${L.win}</th><th>${L.left}</th><th>${L.reset}</th></tr>`;
 for(const p of u.p){if(!p.w.length)h+=`<tr><td>${p.n}</td><td colspan=3>${p.err}</td></tr>`;
  p.w.forEach((w,i)=>{h+=`<tr><td>${i?"":p.n}</td><td>${w.l}</td><td>${w.u<0?"-":100-w.u+"%"}</td><td>${w.r&&u.now?dur(w.r-u.now):"-"}</td></tr>`})}
 $("usage").innerHTML=h;$("age").textContent=L.ago(u.age_s)}catch(e){}}
function apply(){L=T[S.lang]||T.en;document.documentElement.lang=S.lang;document.dir=S.lang=="ar"?"rtl":"ltr";$("lang").value=S.lang;texts();usage()}
async function load(){S=await(await fetch("/api/settings")).json();apply()}
async function save(o){const r=await fetch("/api/settings",{method:"POST",body:JSON.stringify(o)});
 if(r.status==401){$("loginbox").hidden=false;$("lp").focus();$("msg").textContent=L.auth;return}S=await r.json();apply();$("msg").textContent=L.saved}
async function setPass(){const p=$("np").value;if(!p)return;$("np").value="";await save({pass:p});load()}
async function login(){const r=await fetch("/api/login",{method:"POST",body:JSON.stringify({pass:$("lp").value})});$("lp").value="";
 if(!r.ok){$("lmsg").textContent=(A[S.lang]||A.en)[6];return}$("lmsg").textContent="";
 if(location.search.includes("login")){location.href="/update";return}$("loginbox").hidden=true;load()}
$("lp").onkeydown=e=>{if(e.key=="Enter")login()};
let wsel="";
async function scan(){$("nets").textContent="...";const r=await fetch("/api/wifi/scan");const n=await r.json();$("nets").innerHTML="";
 n.forEach(x=>{const b=document.createElement("button");b.textContent=`${x.ssid} (${x.rssi} dBm)${x.open?"":" \u{1F512}"}`;
  b.onclick=()=>{wsel=x.ssid;$("w_ssid").textContent=x.ssid;$("wform").hidden=false;$("wp").focus()};$("nets").appendChild(b)})}
async function joinWifi(){const r=await fetch("/api/wifi",{method:"POST",body:JSON.stringify({ssid:wsel,pass:$("wp").value})});
 if(r.status==401){$("loginbox").hidden=false;$("msg").textContent=L.auth;return}
 $("wmsg").textContent=(W[S.lang]||W.en)[5];$("wform").hidden=true;$("nets").innerHTML=""}
$("wp").onkeydown=e=>{if(e.key=="Enter")joinWifi()};
$("lang").onchange=e=>save({lang:e.target.value});
async function search(){const q=$("q").value.trim();if(!q)return;
 const r=await(await fetch(`https://geocoding-api.open-meteo.com/v1/search?count=8&format=json&language=${S.lang.split("-")[0]}&name=${encodeURIComponent(q)}`)).json();
 const res=r.results||[];$("results").innerHTML=res.length?"":L.nores;
 res.forEach(p=>{const b=document.createElement("button");const name=[p.name,p.admin1,p.country].filter(Boolean).join(", ");
  b.textContent=name;b.onclick=()=>{$("results").innerHTML="";save({city:p.name,lat:p.latitude,lon:p.longitude})};$("results").appendChild(b)})}
$("search").onclick=search;$("q").onkeydown=e=>{if(e.key=="Enter")search()};
load();setInterval(usage,30000);
</script></body></html>)HTML";
