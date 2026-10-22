# Beginner's Installation & Setup Guide

Welcome! This guide walks you step-by-step through assembling and installing the **WiiM Touchscreen Remote**. 

**No soldering and no programming skills are required**—you do not need to edit any code files. Once the software is loaded onto the screen, you will set up your Wi-Fi password, select your WiiM streamer, and pick your favorite look directly on the touchscreen!

---

## 1. What You Need (Shopping List)

1. **Touchscreen Board**: [Hosyond 2.8" ESP32-S3 Capacitive Touch Display (`240x320` IPS)](https://www.amazon.com/dp/B0FKG7WRWV)
2. **Rechargeable Battery**: [3.7V 3000mAh Li-Po Battery with JST 1.25mm 2-Pin Plug](https://www.amazon.com/dp/B0DK5BBKM5)
3. **USB-C Data Cable**: Any standard USB-C cable that supports **data transfer** (note: some cheap cables that come with power banks or lamps are "charge-only" and won't let your computer talk to the board).
4. **A Computer with Google Chrome or Microsoft Edge**: Windows, Mac, or Linux (no software installation required!).

---

## 2. Connecting the Battery (2 Minutes)

Before plugging the battery into the back of the display board, **always check the wire colors**!

> [!CAUTION]
> **Important: Check Battery Wire Polarity (`+` and `-`) First!**
> Look closely at the small white 2-pin `BAT` socket on the back of the display board. You will see tiny **`+`** and **`-`** markings printed on the circuit board next to the socket:
> - The **Red wire** must go into the **`+`** side.
> - The **Black wire** must go into the **`-`** side.
>
> *Some Li-Po batteries come wired backwards from the factory.* If the Red and Black wires on your battery plug are reversed compared to the `+` and `-` markings on the board, **do not plug it in yet**! Instead, gently lift the two tiny plastic tabs on the white battery plug using a sewing needle or tweezers, slide the two metal pins out, swap them so **Red matches `+`** and **Black matches `-`**, and click them back in.

1. Once you have confirmed **Red = `+`** and **Black = `-`**, gently push the white battery plug into the `BAT` socket on the back of the board.
2. Locate the tiny slide switch next to the battery socket on the edge of the board and slide it to **`ON`**.
   - **Charging**: Whenever you plug a USB-C cable into the board, it automatically recharges the battery.

---

## 3. Installing the Remote Software on the Board (No Software Install Required!)

You only need to do this once, and it works right inside your web browser (**Google Chrome** or **Microsoft Edge**)!

### Step 1: Download the Ready-to-Flash Firmware File
1. Click here to download **[`wiimremote-factory.bin`](https://github.com/guipel/wiimremote/releases/latest/download/wiimremote-factory.bin)** directly (or visit the **[Latest GitHub Release page](https://github.com/guipel/wiimremote/releases/latest)** and click **`wiimremote-factory.bin`** under **Assets**).
2. Save the file to your computer (it will go to your `Downloads` folder).

### Step 2: Plug In Your Board & Open the Web Flasher
1. Plug your ESP32-S3 display board into your computer using your **USB-C data cable**.
2. Open **Google Chrome** or **Microsoft Edge** on your computer and go to the official Espressif Web Flasher:
   👉 **[https://espressif.github.io/esptool-js/](https://espressif.github.io/esptool-js/)**
3. Under the **Program** section at the top of the page, click the **`Connect`** button.
4. A small browser pop-up will appear listing your computer's USB ports. Select the port that says **`USB JTAG/serial debug unit`** (or `USB Serial` / `COM...` / `cu.usbmodem...`) and click **Connect**.

### Step 3: Select the File & Click Program!
1. In the table under **Program**:
   - Change the **Flash Address** box on the left from `0x1000` to **`0x0`** *(make sure it is **`0x0`**, not `0x1000`!)*.
   - Click **`Choose File`** on the right and select the **`wiimremote-factory.bin`** file you downloaded in Step 1.
2. Click the **`Program`** button!
3. You will see a progress bar (`Writing at 0x000...`) for about **15 to 20 seconds**.
4. Once the console box at the bottom says **`Leaving... Hard resetting via RTS pin...`**, press the tiny **`RST`** (Reset) button on the back edge of your display board (or simply unplug and replug the USB-C cable). Your WiiM Remote will immediately light up!

---

## 4. First-Time Setup on the Touchscreen

Once the screen turns on, you can unplug it from your computer or leave it plugged in to charge. Everything else happens right on the touchscreen:

### 1. Connect to Your Home Wi-Fi
- The first time the remote boots, it automatically opens the **Wi-Fi Networks** screen and scans for nearby networks.
- Tap your **2.4 GHz home Wi-Fi network** from the list.
- Use the on-screen touchscreen keyboard to type your Wi-Fi password:
  - Tap **`ABC`** / **`abc`** to switch between uppercase and lowercase letters.
  - Tap **`1#`** to type numbers and special symbols.
  - Tap the **Eye icon** on the right side of the password box if you want to double-check what you typed.
- Tap the **Checkmark (`✓`)** key in the bottom-right corner of the keyboard to connect!

### 2. Select Your WiiM Streamer
- Once connected to Wi-Fi, the remote automatically searches your home network for WiiM devices (`WiiM Mini`, `WiiM Pro`, `WiiM Pro Plus`, `WiiM Amp`, `WiiM Ultra`) and connects to the first one it finds.
- If you have more than one WiiM device in your home, simply **tap the device name at the top center of the screen** to open your device list and switch rooms anytime.

### 3. Customize Your Screen (Themes, Rotation & Sleep)
Tap the **Battery Icon** in the top-right corner of the screen to open the **Power & Display** menu:
- **Theme**: Tap **`Theme: ...`** to cycle live through **6 custom hi-fi color palettes** (*Cyber Cyan*, *Tube Amber*, *Naim Emerald*, *McIntosh Blue*, *Velvet Rose*, and *Studio Crimson*).
- **Screen Rotation (`0°` / `180°`)**: Tap **`Rotation: ...`** to flip the entire screen and touchscreen upside down (`180°`)—handy if you want the USB-C charging cable pointing out the top instead of the bottom!
- **Brightness**: Tap **`Brightness: ...`** to cycle between `20%`, `40%`, `60%`, `80%`, and `100%`.
- **Auto-Dim & Auto-Sleep**: Choose how long the screen waits before dimming (`15s` to `5m`, or `Never`) and going into deep sleep (`30s` to `10m`, or `Never`) to save battery.
- **Wake with a Tap**: Whenever the remote is dimmed or asleep, just **tap anywhere on the screen** to wake it right back up!

---

## 5. Quick Tour of the 4 Tabs

Along the bottom of the screen, you have 4 tabs:
1. **Lyrics**: Fetches song lyrics automatically when you open the tab while a song is playing.
2. **Player**: Shows the scrolling song title, artist, album, audio quality (`24/96`), seek bar, Play/Pause/Skip buttons, volume slider, Mute button, and Input Source switcher (`Wi-Fi`, `Bluetooth`, `AUX`, `OPT`).
3. **Presets**: Lists your 12 WiiM presets—tap any preset to start playing it immediately.
4. **UPnP**: Automatically finds local media servers on your home network (such as *Asset UPnP*, *MinimServer*, *Plex*, or a *Synology NAS*) so you can browse your folders and play entire albums gaplessly.

---

## 6. Troubleshooting FAQ

- **My Wi-Fi network doesn't show up in the scan list!**
  - Like most smart home devices, the ESP32-S3 chip connects to **2.4 GHz Wi-Fi networks** (not 5 GHz-only networks). Make sure your router has 2.4 GHz enabled.
- **No USB port shows up when I click `Connect` in the Web Flasher!**
  - **Use Chrome or Edge**: Make sure you are using **Google Chrome** or **Microsoft Edge** (Safari and Firefox do not support USB WebSerial).
  - **Check your USB-C cable**: Try a different USB-C cable—many cables included with small gadgets only have power wires inside and no data wires.
  - **Manual Boot Mode (if needed)**: On the back/side of the board, there are two tiny buttons labeled **`BOOT`** and **`RST`** (Reset). Hold down **`BOOT`**, press and release **`RST`** once, then let go of **`BOOT`**. Now click **`Connect`** in the Web Flasher again.
- **I flashed the file, pressed `RST`, and the screen didn't turn on!**
  - Double-check that the **Flash Address** in the Web Flasher was set to **`0x0`** (the page defaults to `0x1000`, which will not boot). Set Flash Address to **`0x0`** and click **`Program`** again.
- **The screen Doesn't Turn On When Unplugged from USB-C.**
  - Check that the tiny slide switch next to the white battery plug on the board is switched to **`ON`**, and give the battery 30–60 minutes on USB-C power to charge up.
