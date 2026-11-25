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
| `DISCORD_USERNAME`                       | `ESP32-CAM` | Tên hiển thị của webhook khi gửi tin                                                 |
| `MOTION_ENABLED`                         | `true`      | Bật/tắt phát hiện chuyển động                                                        |
| `MOTION_MIN_AREA`                        | `600`       | Diện tích (điểm ảnh) tối thiểu của vùng thay đổi. Tăng nếu báo nhầm, giảm nếu bỏ sót |
| `MOTION_THRESHOLD`                       | `25`        | Độ lệch sáng (0-255) để tính là thay đổi                                             |
| `MOTION_MIN_FRAMES`                      | `2`         | Số khung liên tiếp có chuyển động mới báo                                            |
| `MOTION_COOLDOWN`                        | `30`        | Giây tối thiểu giữa hai thông báo                                                    |
| `MAX_EVENT_FILES`                        | `200`       | Số ảnh sự kiện giữ lại                                                               |
| `SAVE_UPLOADS`                           | `false`     | Lưu mọi ảnh gửi tới `/upload`                                                        |
| `PORT`                                   | `5000`      | Cổng của server                                                                      |

## Lưu ý bảo mật

Server không có đăng nhập. Chỉ chạy trong mạng LAN tin cậy và không mở cổng
5000 ra Internet.
