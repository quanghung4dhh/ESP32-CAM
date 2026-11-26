# Server ESP32-CAM: phát hiện chuyển động + thông báo

Server kéo (pull) video MJPEG từ ESP32-CAM, so sánh khung hình để phát hiện
chuyển động, lưu ảnh và gửi qua Telegram / Discord, đồng thời phát lại stream
cho trình duyệt (nhiều người xem cùng lúc được).

## Chạy

```bash
cd server
pip install -r requirements.txt

export ESP_STREAM_URL="http://<IP-ESP32>:81/stream"
export TELEGRAM_BOT_TOKEN="<token từ @BotFather>"
export TELEGRAM_CHAT_ID="<chat id của bạn>"
export DISCORD_WEBHOOK_URL="https://discord.com/api/webhooks/<id>/<token>"
python app.py
```

Telegram và Discord đều tùy chọn: bật cái nào thì đặt biến của cái đó, có thể
bật cả hai (thông báo được gửi song song). Nên đặt các biến này trong file
`server/.env` (xem `.env.example`) thay vì `export`.

Mở `http://<IP-máy-chạy-server>:5000/` để xem video, chỉnh zoom và xem các
ảnh chuyển động gần đây.

> Khi server đang giữ kết nối stream của ESP32, trang điều khiển của chính
> ESP32 (cổng 80) sẽ không hiện được video (ESP32 chỉ phục vụ một người xem
> stream). Hãy xem video ở trang của server.

## Lưu ảnh

Ảnh nằm trong `server/captured_images/`:

- `motion_YYYYMMDD_HHMMSS.jpg`: ảnh lúc phát hiện chuyển động (có khung đánh dấu).
- `snapshot_YYYYMMDD_HHMMSS.jpg`: ảnh bấm **📸 Chụp ngay** trên dashboard
  (ảnh gốc, không khung, không gửi Telegram/Discord).

Khi khởi động lại, server đọc lại các ảnh này nên dashboard vẫn hiện 24 ảnh
gần nhất. Ảnh bị xóa khi vượt `MAX_EVENT_FILES` hoặc cũ hơn `MAX_EVENT_DAYS`
(kiểm tra mỗi giờ và mỗi lần có ảnh mới). Muốn giữ ảnh vĩnh viễn thì đặt
`MAX_EVENT_DAYS=0` và `MAX_EVENT_FILES=0`, hoặc tự sao chép ảnh đi chỗ khác.
Chụp tay cũng có thể gọi bằng `curl -X POST http://<IP>:5000/api/snapshot`.

## Tạo bot Telegram

1. Chat với `@BotFather`, gửi `/newbot`, làm theo hướng dẫn để lấy **token**.
2. Gửi một tin nhắn bất kỳ cho bot vừa tạo.
3. Mở `https://api.telegram.org/bot<TOKEN>/getUpdates`, tìm `"chat":{"id":...}`
   để lấy **chat id**.

Không đưa token vào code hay commit lên GitHub: chỉ đặt qua biến môi trường.

## Tạo webhook Discord

1. Trong server Discord của bạn (cần quyền _Manage Webhooks_), mở
   **Server Settings → Integrations → Webhooks → New Webhook**
   (hoặc chuột phải vào kênh → **Edit Channel → Integrations → Webhooks**).
2. Đặt tên, chọn **kênh** nhận ảnh, rồi bấm **Copy Webhook URL**.
3. Dán vào `DISCORD_WEBHOOK_URL` (trong `server/.env`).

Webhook URL cho phép bất kỳ ai có nó gửi tin vào kênh của bạn, nên giữ bí mật
như mật khẩu và không commit lên GitHub (`.env` đã nằm trong `.gitignore`).
Nếu lộ URL, vào lại mục Webhooks và xóa rồi tạo cái mới.

## Lệnh qua Discord (bot)

Webhook ở trên chỉ **gửi** cảnh báo vào kênh. Muốn **điều khiển** camera bằng
lệnh `/snapshot`, `/status`, `/on`, `/off`, `/help` thì cần thêm một Discord bot
(miễn phí). Bot kết nối ra ngoài tới Discord nên không cần mở cổng hay IP công
cộng. Bạn có thể dùng cả webhook lẫn bot.

1. Vào <https://discord.com/developers/applications> → **New Application**, đặt tên.
2. Mục **Bot** → **Reset Token** → sao chép token → đặt vào `DISCORD_BOT_TOKEN`.
   Không cần bật mục _Privileged Gateway Intents_ nào.
3. Mục **OAuth2 → URL Generator**: tick scope `bot` và `applications.commands`;
   ở _Bot Permissions_ tick `Send Messages` và `Attach Files`. Mở URL tạo ra,
   chọn server của bạn, bấm **Authorize**.
4. Bật **Developer Mode**: _User Settings → Advanced → Developer Mode_. Sau đó
   chuột phải vào **icon server** → _Copy Server ID_ → `DISCORD_GUILD_ID`;
   chuột phải vào **tên của bạn** → _Copy User ID_ → `DISCORD_ALLOWED_USER_IDS`.
5. Chạy `pip install -r requirements.txt`, rồi `python app.py`. Log phải có
   `[discord] Registered 5 slash commands` và `[discord] Bot connected as ...`.
6. Trong server Discord gõ `/` sẽ thấy các lệnh của bot.

Chỉ người có ID trong `DISCORD_ALLOWED_USER_IDS` dùng được lệnh; người khác bị
từ chối. Câu trả lời (kể cả ảnh) chỉ mình bạn thấy (_ephemeral_). Không có
`DISCORD_GUILD_ID` thì lệnh được đăng ký toàn cục và có thể mất tới 1 giờ mới
hiện. Token bot là bí mật như mật khẩu: không commit, không gửi cho ai; nếu lộ,
bấm **Reset Token** trong Developer Portal.

| Lỗi trong log                                  | Nguyên nhân                                              |
| ---------------------------------------------- | -------------------------------------------------------- |
| `Bot disabled: missing DISCORD_BOT_TOKEN`      | Chưa đặt token                                           |
| `Bot disabled: discord.py is not installed`    | Chưa chạy `pip install -r requirements.txt`              |
| `Bot disabled: Missing DISCORD_ALLOWED_USER_IDS` | Chưa đặt ID người dùng được phép                       |
| `Login failed: DISCORD_BOT_TOKEN is wrong`     | Token sai hoặc đã reset                                  |
| `Bot error: ..., retry in Ns`                  | Mất mạng hoặc Discord lỗi; bot tự thử lại                |
| Gõ `/` không thấy lệnh                         | Chưa mời bot với scope `applications.commands`, hoặc sai `DISCORD_GUILD_ID` |

## Thử thông báo

Không cần chờ có chuyển động, chạy:

```bash
cd server
python notifier.py --test
```

Lệnh gửi một ảnh thử tới mọi dịch vụ đã cấu hình và in kết quả
(`[notify] Discord: sent` / `[notify] Telegram: sent`). Lỗi thường gặp:

| Thông báo                              | Nguyên nhân                               |
| -------------------------------------- | ----------------------------------------- |
| `Discord HTTP 404` hoặc `401`          | Webhook URL sai hoặc đã bị xóa            |
| `Discord rate limited`                 | Gửi quá nhanh; server tự chờ rồi thử lại  |
| `Discord HTTP 413`                     | Ảnh quá lớn (giới hạn upload của Discord) |
| `Telegram HTTP 400 ... chat not found` | Chưa nhắn cho bot trước, hoặc sai chat id |

## Biến môi trường

| Biến                                     | Mặc định    | Ý nghĩa                                                                              |
| ---------------------------------------- | ----------- | ------------------------------------------------------------------------------------ |
| `ESP_STREAM_URL`                         | (trống)     | Địa chỉ stream của ESP32, ví dụ `http://192.168.4.122:81/stream`                     |
| `ESP_CONTROL_URL`                        | tự suy ra   | Địa chỉ điều khiển zoom, mặc định `http://<IP>/control`                              |
| `TELEGRAM_BOT_TOKEN`, `TELEGRAM_CHAT_ID` | (trống)     | Bật thông báo Telegram                                                               |
| `DISCORD_WEBHOOK_URL`                    | (trống)     | Bật thông báo Discord (xem "Tạo webhook Discord")                                    |
| `DISCORD_BOT_TOKEN`, `DISCORD_GUILD_ID`, `DISCORD_ALLOWED_USER_IDS` | (trống) | Bật bot nhận lệnh Discord (xem "Lệnh qua Discord") |
| `DISCORD_USERNAME`                       | `ESP32-CAM` | Tên hiển thị của webhook khi gửi tin                                                 |
| `MOTION_ENABLED`                         | `true`      | Bật/tắt phát hiện chuyển động                                                        |
| `MOTION_MIN_AREA`                        | `600`       | Diện tích (điểm ảnh) tối thiểu của vùng thay đổi. Tăng nếu báo nhầm, giảm nếu bỏ sót |
| `MOTION_THRESHOLD`                       | `25`        | Độ lệch sáng (0-255) để tính là thay đổi                                             |
| `MOTION_MIN_FRAMES`                      | `2`         | Số khung liên tiếp có chuyển động mới báo                                            |
| `MOTION_COOLDOWN`                        | `30`        | Giây tối thiểu giữa hai thông báo                                                    |
| `MAX_EVENT_FILES`                        | `200`       | Số ảnh tối đa giữ lại (ảnh chuyển động + ảnh chụp tay); ảnh cũ nhất bị xóa trước     |
| `MAX_EVENT_DAYS`                         | `7`         | Xóa ảnh cũ hơn số ngày này (`0` = không xóa theo tuổi)                               |
| `SAVE_UPLOADS`                           | `false`     | Lưu mọi ảnh gửi tới `/upload`                                                        |
| `PORT`                                   | `5000`      | Cổng của server                                                                      |

## Cấu trúc mã nguồn

Chạy bằng `python app.py` (trong thư mục `server/`). Mỗi file một nhiệm vụ,
khi gặp lỗi chỉ cần mở đúng file:

| File                  | Nhiệm vụ                                                                  |
| --------------------- | ------------------------------------------------------------------------- |
| `app.py`              | Điểm vào: tạo Flask, in thông tin khởi động, bật stream và bot Telegram   |
| `settings.py`         | Đọc biến môi trường / `.env`, đường dẫn thư mục ảnh                       |
| `runtime.py`          | Đối tượng dùng chung: kho khung hình, notifier, bộ phát hiện, trạng thái  |
| `motion_pipeline.py`  | Mỗi khung hình mới: phát hiện chuyển động, lưu ảnh, gửi cảnh báo          |
| `event_store.py`      | Ảnh đã lưu trên đĩa, dọn ảnh cũ, danh sách sự kiện của dashboard          |
| `camera_ops.py`       | Chụp ảnh, bật/tắt phát hiện chuyển động, lấy trạng thái hệ thống          |
| `web_routes.py`       | Các đường dẫn HTTP (`/`, `/status`, `/api/...`, `/stream`, `/upload`)     |
| `web_page.py`         | HTML của dashboard                                                        |
| `telegram_bot.py`     | Nhận lệnh Telegram (long polling), chỉ nghe đúng `TELEGRAM_CHAT_ID`       |
| `system_controller.py`| Chuyển lệnh Telegram thành thao tác (`/snapshot`, `/on`, `/off`, ...)     |
| `discord_bot.py`      | Nhận lệnh Discord (slash commands), chỉ nghe `DISCORD_ALLOWED_USER_IDS`    |
| `notifier.py`         | Gửi ảnh tới Telegram / Discord (`python notifier.py --test`)              |
| `detector.py`         | Thuật toán phát hiện chuyển động (OpenCV)                                 |
| `stream_client.py`    | Kéo stream MJPEG từ ESP32-CAM và giữ khung hình mới nhất                  |

## Lưu ý bảo mật

Server không có đăng nhập. Chỉ chạy trong mạng LAN tin cậy và không mở cổng
5000 ra Internet.
