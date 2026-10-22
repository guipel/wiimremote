# Beginner's Installation & Setup Guide

Welcome! This guide walks you step-by-step through assembling and installing the **WiiM Touchscreen Remote**. 

**No soldering and no programming skills are required**—you do not need to edit any code files. Once the software is loaded onto the screen, you will set up your Wi-Fi password, select your WiiM streamer, and pick your favorite look directly on the touchscreen!

---

## 1. What You Need (Shopping List)

1. **Touchscreen Board**: [Hosyond 2.8" ESP32-S3 Capacitive Touch Display (`240x320` IPS)](https://www.amazon.com/dp/B0FKG7WRWV)
2. **Rechargeable Battery**: [3.7V 3000mAh Li-Po Battery with JST 1.25mm 2-Pin Plug](https://www.amazon.com/dp/B0DK5BBKM5)
3. **USB-C Data Cable**: Any standard USB-C cable that supports **data transfer** (note: some cheap cables that come with power banks or lamps are "charge-only" and won't let your computer talk to the board).
4. **A Computer**: Windows, Mac, or Linux.

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

## 3. Installing the Remote Software on the Board

You only need to do this once!

### Step 1: Install Visual Studio Code (Free)
1. Download and install **[Visual Studio Code](https://code.visualstudio.com/)** for your computer (Windows, Mac, or Linux).
2. Open Visual Studio Code once it finishes installing.

### Step 2: Add the "PlatformIO" Helper Extension
1. Look at the vertical bar on the far-left side of Visual Studio Code and click the **Extensions** icon (it looks like **4 little squares**).
2. In the search box at the top, type **`PlatformIO IDE`**.
3. Click the blue **Install** button next to **PlatformIO IDE** (by PlatformIO).
4. **Wait 1 to 2 minutes** while it installs in the background. When it is finished, you will see a tiny **Alien Ant head icon** appear on the left sidebar and a row of small icons (a house, a checkmark, and a right-pointing arrow `→`) along the **bottom blue bar** of the window.

### Step 3: Download & Open This Project
1. On the main GitHub page for this project, click the green **`<> Code`** button and choose **Download ZIP** (or clone it if you use Git).
2. **Unzip (Extract)** the downloaded `.zip` file to a normal folder on your computer (for example, your `Documents` or `Desktop` folder).
3. In Visual Studio Code, click **`File` → `Open Folder...`** in the top menu and select the unzipped **`wiimremote`** folder.
   - *(If a prompt asks "Do you trust the authors of the files in this folder?", click **Yes, I trust the authors**).*

### Step 4: Plug In & Click Upload!
1. Plug your ESP32-S3 display board into your computer using your **USB-C data cable**.
2. Wait about 30 seconds for PlatformIO to finish loading the project (you will see status messages in the bottom bar).
3. Look at the **bottom blue status bar** of Visual Studio Code and click the **Right Arrow (`→`)** icon (if you hover your mouse over it, it says **`PlatformIO: Upload`**).
4. PlatformIO will automatically download all required display libraries, build the software, and send it to your screen. This takes about **1 to 2 minutes** the first time.
5. When you see **`[SUCCESS]`** in the terminal panel at the bottom, your WiiM Remote will restart automatically and light up!

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
- **Clicking Upload (`→`) in VS Code says "Could not open port" or "No serial port found".**
  - **Check your USB-C cable**: Try a different USB-C cable—many cables included with small gadgets only have power wires inside and no data wires.
  - **Manual Boot Mode (if needed)**: On the back/side of the board, there are two tiny buttons labeled **`BOOT`** and **`RST`** (Reset). Hold down **`BOOT`**, press and release **`RST`** once, then let go of **`BOOT`**. Now click the **Upload (`→`)** arrow in VS Code again.
- **The screen Doesn't Turn On When Unplugged from USB-C.**
  - Check that the tiny slide switch next to the white battery plug on the board is switched to **`ON`**, and give the battery 30–60 minutes on USB-C power to charge up.
