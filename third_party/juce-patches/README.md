# Patches to JUCE 9.0.2

Lesson EQ and Lesson Comp build against JUCE 9.0.2 with the patches in this folder. They are copies of the patches of
subbass-vst (`D:\work\Claude\subbass-vst\third_party\juce-patches`), and the JUCE tree is the one subbass-vst uses. The
top `CMakeLists.txt` stops with an error when `LESSON_JUCE_DIR` lacks them.

| Patch | What it does |
| --- | --- |
| `0001-webview2-text-scale.patch` | Adds `WebBrowserComponent::Options::WinWebView2::withTextScaleIgnored()`: the webview is rasterised at the monitor's DPI scale alone, without the Windows text size, so the page does not overflow the fixed-size editor. |
| `0002-webview2-page-at-component-size.patch` | With the same option, the page is laid out at the component's size, and a transform on the component or a parent scales the page with it. Also turns off the page's own zoom (Ctrl + wheel, Ctrl + plus/minus). |

## Setting up

The default `LESSON_JUCE_DIR` is `D:/work/Claude/_sdk/JUCE/JUCE-subbass`, a git worktree of the shared JUCE checkout
(`D:/work/Claude/_sdk/JUCE/JUCE`, which stays unpatched). When it does not exist yet:

```bash
git -C D:/work/Claude/_sdk/JUCE/JUCE worktree add ../JUCE-subbass -b subbass/webview2-text-scale 72782788ce
git -C D:/work/Claude/_sdk/JUCE/JUCE-subbass am "$PWD/third_party/juce-patches/0001-webview2-text-scale.patch"
git -C D:/work/Claude/_sdk/JUCE/JUCE-subbass am "$PWD/third_party/juce-patches/0002-webview2-page-at-component-size.patch"
```

`72782788ce` is the commit "JUCE version 9.0.2".
