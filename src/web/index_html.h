#ifndef INDEX_HTML_H
#define INDEX_HTML_H

static const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">

  <title>ESP32-CAM Control Panel</title>

  <style>
    * {
      box-sizing: border-box;
    }

    body {
      margin: 0;
      padding: 24px;
      font-family: Arial, sans-serif;
      background: #111827;
      color: #f9fafb;
    }

    .container {
      max-width: 1100px;
      margin: auto;
    }

    header {
      margin-bottom: 24px;
    }

    header h1 {
      margin-bottom: 8px;
    }

    .status {
      color: #4ade80;
      font-size: 14px;
    }

    .layout {
      display: grid;
      grid-template-columns: 2fr 1fr;
      gap: 20px;
    }

    .card {
      background: #1f2937;
      border-radius: 12px;
      padding: 20px;
    }

    .preview {
      width: 100%;
      min-height: 240px;
      background: #000;
      border-radius: 8px;
      display: flex;
      align-items: center;
      justify-content: center;
      overflow: hidden;
    }

    .preview img {
      display: block;
      width: 100%;
      height: auto;
      max-height: 65vh;
      object-fit: contain;
    }

    .setting {
      margin: 20px 0;
      padding-bottom: 16px;
      border-bottom: 1px solid #374151;
    }

    .setting label {
      display: block;
      margin-bottom: 8px;
      font-weight: bold;
    }

    input, select {
      width: 100%;
    }

    input:disabled,
    select:disabled {
      opacity: 0.5;
    }

    .note {
      color: #9ca3af;
      font-size: 13px;
      line-height: 1.5;
    }

    @media (max-width: 750px) {
      body {
        padding: 14px;
      }

      .layout {
        grid-template-columns: 1fr;
      }
    }
  </style>
  
</head>

<body>
  <div class="container">

    <header>
      <h1>ESP32-CAM Control Panel</h1>
      <div class="status">
        ● Camera streaming
      </div>
    </header>

    <div class="layout">

      <section class="card">

<div class="setting">
  <label for="zoomRange">
    Digital Zoom:
    <span id="zoomValue">1.0×</span>
  </label>

  <input
  type="range"
  id="zoomRange"
  min="1.0"
  max="4.0"
  step="0.1"
  value="1.0"
  oninput="
    document.getElementById('zoomValue').textContent =
    Number(this.value).toFixed(1) + '×';
  "
  onchange="sendZoom(this.value)"
>

  <p class="note">
    Adjust digital zoom from 1.0× to 4.0×.
  </p>
</div>

        <h2>Live Preview</h2>

        <div class="preview">
          <img id="cameraStream"
  alt="Camera Stream"
  style="width: 100%; height: auto;">
        </div>

        <p class="note">
          Live MJPEG stream from ESP32-CAM.
        </p>
      </section>

      <section class="card">
        <h2>Camera Settings</h2>

        <div class="setting">
          <label>JPEG Quality</label>
          <input type="range" min="10" max="90"
                 value="80" disabled>
          <p class="note">
            Available in the next development step.
          </p>
        </div>

        <div class="setting">
          <label>Brightness</label>
          <input type="range" min="-2" max="2"
                 value="0" disabled>
        </div>

        <div class="setting">
          <label>Contrast</label>
          <input type="range" min="-2" max="2"
                 value="0" disabled>
        </div>

        <div class="setting">
          <label>Resolution</label>
          <select disabled>
            <option>QVGA (320x240)</option>
          </select>
        </div>

        <p class="note">
          Camera controls will be enabled
          after the control API is implemented.
        </p>
      </section>

    </div>
  </div>
  <script>
  // Initialize camera stream
  const streamUrl =
    'http://' + window.location.hostname + ':81/stream';

  document.getElementById('cameraStream').src = streamUrl;


  // Send zoom command to ESP32
  async function sendZoom(value) {
    try {
      const response = await fetch(
        `/control?zoom=${encodeURIComponent(value)}`
      );

      const message = await response.text();

      if (!response.ok) {
        throw new Error(message);
      }

      console.log(message);

    } catch (error) {
      console.error('Zoom control failed:', error);
    }
  }
</script>
</body>
</html>
)rawliteral";

#endif