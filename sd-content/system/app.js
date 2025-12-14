(() => {
  const PREVIEW_W = 800;
  const PREVIEW_H = 480;

  const fileEl = document.getElementById('file');
  const uploadBtn = document.getElementById('upload');
  const statusEl = document.getElementById('status');
  const rotationEl = document.getElementById('rotation');

  const previewCanvas = document.getElementById('preview');
  const previewCtx = previewCanvas.getContext('2d', { willReadFrequently: true });

  // Work canvas for cropping/dithering and BMP generation.
  // Its dimensions depend on rotation:
  // - 0/180: 800x480
  // - 90/270: 480x800
  const workCanvas = document.createElement('canvas');
  const workCtx = workCanvas.getContext('2d', { willReadFrequently: true });

  let loadedImage = null;

  // Ensure preview canvas is stable.
  previewCanvas.width = PREVIEW_W;
  previewCanvas.height = PREVIEW_H;

  const palette = [
    { name: 'black', r: 0,   g: 0,   b: 0   },
    { name: 'white', r: 255, g: 255, b: 255 },
    { name: 'yellow',r: 255, g: 255, b: 0   },
    { name: 'red',   r: 255, g: 0,   b: 0   },
    { name: 'blue',  r: 0,   g: 0,   b: 255 },
    { name: 'green', r: 0,   g: 255, b: 0   },
  ];

  function setStatus(msg) {
    statusEl.textContent = msg;
  }

  function normalizeRotation(deg) {
    const d = Number(deg);
    return (d === 0 || d === 90 || d === 180 || d === 270) ? d : 180;
  }

  function getRotationDeg() {
    return normalizeRotation(rotationEl.value);
  }

  function getTargetSize(deg) {
    const d = normalizeRotation(deg);
    if (d === 90 || d === 270) {
      return { w: 480, h: 800 };
    }
    return { w: 800, h: 480 };
  }

  function ensureWorkCanvasSize(deg) {
    const { w, h } = getTargetSize(deg);
    if (workCanvas.width !== w) workCanvas.width = w;
    if (workCanvas.height !== h) workCanvas.height = h;
    return { w, h };
  }

  function drawPlaceholder() {
    previewCtx.setTransform(1, 0, 0, 1, 0, 0);
    previewCtx.fillStyle = '#ddd';
    previewCtx.fillRect(0, 0, PREVIEW_W, PREVIEW_H);
    previewCtx.fillStyle = '#333';
    previewCtx.font = '16px system-ui, -apple-system, Segoe UI, Roboto, sans-serif';
    previewCtx.fillText('Select an image to begin', 16, 32);
  }

  function drawPreview(deg) {
    const d = normalizeRotation(deg);
    const { w, h } = ensureWorkCanvasSize(d);

    previewCtx.setTransform(1, 0, 0, 1, 0, 0);
    previewCtx.clearRect(0, 0, PREVIEW_W, PREVIEW_H);
    previewCtx.fillStyle = '#eee';
    previewCtx.fillRect(0, 0, PREVIEW_W, PREVIEW_H);

    previewCtx.save();
    previewCtx.translate(PREVIEW_W / 2, PREVIEW_H / 2);
    previewCtx.rotate(d * Math.PI / 180);
    previewCtx.drawImage(workCanvas, -w / 2, -h / 2);
    previewCtx.restore();
  }

  function processLoadedImage() {
    if (!loadedImage) {
      drawPlaceholder();
      return;
    }

    uploadBtn.disabled = true;

    const deg = getRotationDeg();
    const { w, h } = ensureWorkCanvasSize(deg);

    drawCenterCrop(workCtx, w, h, loadedImage);

    setStatus('Dithering...');
    setTimeout(() => {
      try {
        const imgData = workCtx.getImageData(0, 0, w, h);
        ditherImageDataFS(imgData, w, h);
        workCtx.putImageData(imgData, 0, 0);

        drawPreview(deg);

        uploadBtn.disabled = false;
        setStatus(`Ready (${w}x${h}). Click "Crop + Upload".`);
      } catch (e) {
        setStatus(`Dither failed: ${String(e)}`);
        uploadBtn.disabled = true;
      }
    }, 0);
  }

  async function loadRotationFromDevice() {
    try {
      const res = await fetch('/api/rotation', { cache: 'no-store' });
      if (!res.ok) return;
      const json = await res.json();
      const deg = normalizeRotation(json.rotation);
      rotationEl.value = String(deg);
    } catch (_) {
      // Ignore.
    }
  }

  async function setRotationOnDevice(deg) {
    const d = normalizeRotation(deg);
    try {
      const res = await fetch('/api/rotation', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: String(d),
      });

      const text = await res.text();
      if (!res.ok) {
        setStatus(`Failed to set rotation (HTTP ${res.status}): ${text}`);
        return false;
      }

      // Response is JSON, but be robust if it isn't.
      let saved = d;
      try {
        const json = JSON.parse(text);
        saved = normalizeRotation(json.rotation);
      } catch (_) {
        // Ignore.
      }

      rotationEl.value = String(saved);
      return true;
    } catch (e) {
      setStatus(`Failed to set rotation: ${String(e)}`);
      return false;
    }
  }

  function closestPaletteColor(r, g, b) {
    let best = palette[0];
    let bestD = Infinity;
    for (const p of palette) {
      const dr = r - p.r;
      const dg = g - p.g;
      const db = b - p.b;
      const d = dr*dr + dg*dg + db*db;
      if (d < bestD) {
        bestD = d;
        best = p;
      }
    }
    return best;
  }

  function clamp255(v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
  }

  // Floyd-Steinberg dithering to the 6-color palette.
  function ditherImageDataFS(imgData, w, h) {
    const data = imgData.data;

    let errR = new Float32Array(w + 2);
    let errG = new Float32Array(w + 2);
    let errB = new Float32Array(w + 2);

    let nextErrR = new Float32Array(w + 2);
    let nextErrG = new Float32Array(w + 2);
    let nextErrB = new Float32Array(w + 2);

    for (let y = 0; y < h; y++) {
      for (let x = 0; x < w; x++) {
        const i = (y * w + x) * 4;

        const r0 = data[i + 0];
        const g0 = data[i + 1];
        const b0 = data[i + 2];

        const r = clamp255(r0 + errR[x]);
        const g = clamp255(g0 + errG[x]);
        const b = clamp255(b0 + errB[x]);

        const p = closestPaletteColor(r, g, b);
        data[i + 0] = p.r;
        data[i + 1] = p.g;
        data[i + 2] = p.b;
        data[i + 3] = 255;

        const er = r - p.r;
        const eg = g - p.g;
        const eb = b - p.b;

        // Right pixel
        if (x + 1 < w) {
          errR[x + 1] += er * (7 / 16);
          errG[x + 1] += eg * (7 / 16);
          errB[x + 1] += eb * (7 / 16);
        }

        // Next row
        if (y + 1 < h) {
          if (x > 0) {
            nextErrR[x - 1] += er * (3 / 16);
            nextErrG[x - 1] += eg * (3 / 16);
            nextErrB[x - 1] += eb * (3 / 16);
          }
          nextErrR[x] += er * (5 / 16);
          nextErrG[x] += eg * (5 / 16);
          nextErrB[x] += eb * (5 / 16);
          if (x + 1 < w) {
            nextErrR[x + 1] += er * (1 / 16);
            nextErrG[x + 1] += eg * (1 / 16);
            nextErrB[x + 1] += eb * (1 / 16);
          }
        }
      }

      // Move down a row (swap buffers).
      let tmp;
      tmp = errR; errR = nextErrR; nextErrR = tmp; nextErrR.fill(0);
      tmp = errG; errG = nextErrG; nextErrG = tmp; nextErrG.fill(0);
      tmp = errB; errB = nextErrB; nextErrB = tmp; nextErrB.fill(0);
    }

    return imgData;
  }

  function drawCenterCrop(dstCtx, dstW, dstH, img) {
    // Composite on white to avoid transparency issues.
    dstCtx.fillStyle = '#fff';
    dstCtx.fillRect(0, 0, dstW, dstH);

    const srcW = img.naturalWidth || img.width;
    const srcH = img.naturalHeight || img.height;

    const targetAR = dstW / dstH;
    const srcAR = srcW / srcH;

    let sx = 0, sy = 0, sw = srcW, sh = srcH;
    if (srcAR > targetAR) {
      // Source is wider: crop width.
      sw = Math.round(srcH * targetAR);
      sx = Math.round((srcW - sw) / 2);
    } else {
      // Source is taller: crop height.
      sh = Math.round(srcW / targetAR);
      sy = Math.round((srcH - sh) / 2);
    }

    dstCtx.drawImage(img, sx, sy, sw, sh, 0, 0, dstW, dstH);
  }

  function writeU16(view, off, v) { view.setUint16(off, v, true); }
  function writeU32(view, off, v) { view.setUint32(off, v, true); }

  function buildBmp24FromWorkCanvas() {
    // Work canvas is already dithered to the exact palette.
    const w = workCanvas.width;
    const h = workCanvas.height;

    const img = workCtx.getImageData(0, 0, w, h);
    const data = img.data;

    const rowSizeNoPad = w * 3;
    const rowSize = (rowSizeNoPad + 3) & ~3;
    const padding = rowSize - rowSizeNoPad;

    const pixelBytes = rowSize * h;
    const fileSize = 54 + pixelBytes;

    const buf = new ArrayBuffer(fileSize);
    const bytes = new Uint8Array(buf);
    const view = new DataView(buf);

    // BITMAPFILEHEADER (14 bytes)
    bytes[0] = 0x42; // 'B'
    bytes[1] = 0x4D; // 'M'
    writeU32(view, 2, fileSize);
    writeU32(view, 6, 0);
    writeU32(view, 10, 54);

    // BITMAPINFOHEADER (40 bytes)
    writeU32(view, 14, 40);
    writeU32(view, 18, w);
    writeU32(view, 22, h); // positive = bottom-up
    writeU16(view, 26, 1);
    writeU16(view, 28, 24);
    writeU32(view, 30, 0);
    writeU32(view, 34, pixelBytes);
    writeU32(view, 38, 2835);
    writeU32(view, 42, 2835);
    writeU32(view, 46, 0);
    writeU32(view, 50, 0);

    let out = 54;
    // BMP is bottom-up: write rows from bottom to top.
    for (let y = h - 1; y >= 0; y--) {
      for (let x = 0; x < w; x++) {
        const i = (y * w + x) * 4;
        const r = data[i + 0];
        const g = data[i + 1];
        const b = data[i + 2];
        // BMP pixel format is B, G, R.
        bytes[out++] = b;
        bytes[out++] = g;
        bytes[out++] = r;
      }
      for (let p = 0; p < padding; p++) bytes[out++] = 0;
    }

    return buf;
  }

  rotationEl.addEventListener('change', async () => {
    uploadBtn.disabled = true;

    const deg = getRotationDeg();
    setStatus('Setting rotation on device...');
    const ok = await setRotationOnDevice(deg);
    if (!ok) return;

    processLoadedImage();
  });

  fileEl.addEventListener('change', () => {
    uploadBtn.disabled = true;

    const f = fileEl.files && fileEl.files[0];
    if (!f) {
      loadedImage = null;
      drawPlaceholder();
      setStatus('No file selected.');
      return;
    }

    setStatus('Loading image...');
    const url = URL.createObjectURL(f);
    const img = new Image();
    img.onload = () => {
      loadedImage = img;
      try {
        processLoadedImage();
      } finally {
        URL.revokeObjectURL(url);
      }
    };
    img.onerror = () => {
      loadedImage = null;
      uploadBtn.disabled = true;
      setStatus('Failed to load image.');
      URL.revokeObjectURL(url);
    };
    img.src = url;
  });

  uploadBtn.addEventListener('click', async () => {
    if (!loadedImage) {
      setStatus('No image selected.');
      return;
    }

    uploadBtn.disabled = true;
    try {
      const deg = getRotationDeg();

      setStatus('Setting rotation on device...');
      const ok = await setRotationOnDevice(deg);
      if (!ok) return;

      setStatus('Building BMP...');
      const bmp = buildBmp24FromWorkCanvas();

      setStatus(`Uploading... (${(bmp.byteLength / (1024*1024)).toFixed(2)} MiB)`);
      const res = await fetch('/dataUP', {
        method: 'POST',
        headers: { 'Content-Type': 'image/bmp' },
        body: bmp,
      });

      const text = await res.text();
      setStatus(`Server response: ${text}`);
    } catch (e) {
      setStatus(`Error: ${String(e)}`);
    } finally {
      uploadBtn.disabled = false;
    }
  });

  // Initial render.
  drawPlaceholder();
  (async () => {
    await loadRotationFromDevice();
    processLoadedImage();
  })();
})();
