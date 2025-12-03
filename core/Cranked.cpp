#include "Cranked.hpp"

using namespace cranked;

Cranked::~Cranked() {
    try {
        reset();
    } catch (exception &ex) {
        logMessage(LogLevel::Error, "Failed to clean up: %s", ex.what());
    }
}

void Cranked::init() {
    graphics.init();
    menu.init();
    files.init();
    audio.init();
    nativeEngine.init();
    luaEngine.init();
    debugger.init();
    startTime = elapsedTimeStart = chrono::system_clock::now();
    initialized = true;

    graphics.flushDisplayBuffer(); // Todo: Is this where this is supposed to happen? imagesample example stops prevents first update loop call with playdate.stop
}

void Cranked::load(const string &path) {
    fprintf(stderr, "Cranked::load() called with path: %s\n", path.c_str());
    unload();
    fprintf(stderr, "Cranked::load() unload completed\n");
    rom = make_unique<Rom>(path, this);
    fprintf(stderr, "Cranked::load() Rom created\n");
    rom->load();
    fprintf(stderr, "Cranked::load() rom->load() completed\n");
    nativeEngine.load();
    fprintf(stderr, "Cranked::load() completed\n");
}

void Cranked::unload() {
    reset();
    if (rom)
        rom->unload();
    nativeEngine.unload();
}

void Cranked::reset() {
    state = State::Stopped;
    initialized = false;
    lastFrameTime = {};
    disableUpdateLoop = false;
    crankSoundsEnabled = true;
    statsInterval = 0;

    debugger.reset();
    graphics.reset();
    audio.reset();
    menu.reset();
    files.reset();
    bump.reset();
    luaEngine.reset();
    nativeEngine.reset();

    currentInputs = previousInputs = pressedInputs = releasedInputs = 0;
    // Initialize crank to 90 degrees (not 0) so games know it's been "used"
    // In an emulator, the crank should always appear to be in a valid, usable state
    crankAngle = previousCrankAngle = 90.0f;
    // Set previousCrankDocked = true so first update will trigger "crankUndocked" callback
    // This signals to CoreLibs that crank is available from the start
    crankDocked = false;
    previousCrankDocked = true;

    heap.reset(); // Reset heap only after objects are freed
}

void Cranked::update() {
    // Todo: Handle `Stopping` state and call trigger corresponding events

    if (state != State::Running) {
        updateInternals();
        return;
    }

    auto currentMillisStart = chrono::system_clock::now();

    auto currentTime = chrono::system_clock::now();
    if (graphics.framerate <= 0 or currentTime > lastFrameTime + chrono::milliseconds(int(1000 / graphics.framerate))) {
        lastFrameDelta = (float)chrono::duration_cast<chrono::microseconds>(currentTime - lastFrameTime).count() / 1000.0f;
        lastFrameTime = currentTime;

        pressedInputs = currentInputs & ~previousInputs;
        releasedInputs = ~currentInputs & previousInputs;
        previousInputs = currentInputs;

        if (pressedInputs & int(PDButtons::A))
            buttonDownTimeA = chrono::system_clock::now();
        if (pressedInputs & int(PDButtons::B))
            buttonDownTimeB = chrono::system_clock::now();
        if (releasedInputs & int(PDButtons::A))
            buttonDownTimeA = {};
        if (releasedInputs & int(PDButtons::B))
            buttonDownTimeB = {};

        bool menuOpen = menu.isOpen; // Delay a frame when closing menu to prevent duplicate button inputs

        menu.update();
        graphics.update();

        if (!menuOpen) {
            // Call native input events
            if (nativeEngine.isLoaded()) {
                for (int i = 0; i < 6; i++) {
                    auto bit = 1 << i;
                    if (pressedInputs & bit)
                        nativeEngine.invokeEventCallback(PDSystemEvent::KeyPressed, (uint32)bit);
                    if (releasedInputs & bit)
                        nativeEngine.invokeEventCallback(PDSystemEvent::KeyReleased, (uint32)bit);
                }
            }

            // Call Lua input events
            if (luaEngine.isLoaded()) {
                if (pressedInputs & int(PDButtons::A))
                    luaEngine.invokeLuaInputCallback("AButtonDown");
                if (pressedInputs & int(PDButtons::B))
                    luaEngine.invokeLuaInputCallback("BButtonDown");
                if (pressedInputs & int(PDButtons::Down))
                    luaEngine.invokeLuaInputCallback("downButtonDown");
                if (pressedInputs & int(PDButtons::Left))
                    luaEngine.invokeLuaInputCallback("leftButtonDown");
                if (pressedInputs & int(PDButtons::Right))
                    luaEngine.invokeLuaInputCallback("rightButtonDown");
                if (pressedInputs & int(PDButtons::Up))
                    luaEngine.invokeLuaInputCallback("upButtonDown");

                if (releasedInputs & int(PDButtons::A))
                    luaEngine.invokeLuaInputCallback("AButtonUp");
                if (releasedInputs & int(PDButtons::B))
                    luaEngine.invokeLuaInputCallback("BButtonUp");
                if (releasedInputs & int(PDButtons::Down))
                    luaEngine.invokeLuaInputCallback("downButtonUp");
                if (releasedInputs & int(PDButtons::Left))
                    luaEngine.invokeLuaInputCallback("leftButtonUp");
                if (releasedInputs & int(PDButtons::Right))
                    luaEngine.invokeLuaInputCallback("rightButtonUp");
                if (releasedInputs & int(PDButtons::Up))
                    luaEngine.invokeLuaInputCallback("upButtonUp");

                if (buttonDownTimeA.time_since_epoch() != chrono::system_clock::time_point::duration::zero() and currentTime > buttonDownTimeA + chrono::seconds(1)) {
                    luaEngine.invokeLuaInputCallback("AButtonHeld");
                    buttonDownTimeA = {};
                }
                if (buttonDownTimeB.time_since_epoch() != chrono::system_clock::time_point::duration::zero() and currentTime > buttonDownTimeB + chrono::seconds(1)) {
                    luaEngine.invokeLuaInputCallback("BButtonHeld");
                    buttonDownTimeB = {};
                }

                if (crankAngle != previousCrankAngle) {
                    auto change = crankAngle - previousCrankAngle;
                    luaEngine.invokeLuaInputCallback("cranked", {change, change}); // Todo: `Acceleration`
                    // NOTE: Do NOT update previousCrankAngle here! It must only be updated by getCrankChange()
                    // so that getCrankChange() returns the correct delta when called by the game
                }

                if (crankDocked and not previousCrankDocked)
                    luaEngine.invokeLuaInputCallback("crankDocked");
                if (previousCrankDocked and not crankDocked)
                    luaEngine.invokeLuaInputCallback("crankUndocked");
            }

            if (!disableUpdateLoop and currentTime > suspendUpdateLoopUntil) {
                if (nativeEngine.hasUpdateCallback()) {
                    if (nativeEngine.invokeUpdateCallback())
                        graphics.flushDisplayBuffer();
                } else if (luaEngine.isLoaded()) {
                    luaEngine.runUpdateLoop();
                    graphics.flushDisplayBuffer();
                }
            }
        } else
            graphics.flushDisplayBuffer();

        previousCrankDocked = crankDocked;
    }

    updateInternals();

    // Track frame timing via lastFrameDelta and startTime; do not accumulate CPU time here
    // currentMillis is derived from startTime in playdate_sys_getCurrentTimeMilliseconds

    FrameImage(graphics.displayBufferRGBA, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, false);
    FrameMark; // Todo: This isn't ideal since a single update call can span multiple frames at present
}

void Cranked::start() {
    if (!rom)
        throw CrankedError("Rom not loaded");
    state = State::Running;
    if (!initialized)
        init();
}

void Cranked::stop() {
    if (state != State::Running)
        return;
    state = State::Stopping;
    // Todo: Stop from UC and Lua hooks
    // Todo: Actually set state to Stopped, probably in `update`
}

void Cranked::terminate() {
    state = State::Stopped; // Todo: Maybe a different flag
}

void Cranked::updateInternals() {
    debugger.update();
    if (config.updateCallback)
        config.updateCallback(*this);
}
