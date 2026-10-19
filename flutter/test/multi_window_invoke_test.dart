import 'package:flutter/services.dart';
import 'package:flutter_hbb/utils/multi_window_manager.dart';
import 'package:flutter_test/flutter_test.dart';

// The channel behind DesktopMultiWindow.invokeMethod (desktop_multi_window).
const _channel = MethodChannel('mixin.one/flutter_multi_window_channel');

void mockWindows(Object? Function(int windowId) handler) {
  TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
      .setMockMethodCallHandler(_channel,
          (call) async => handler(call.arguments['targetWindowId'] as int));
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('invokeWindow waits for a window that has no handler yet', () async {
    var calls = 0;
    var failures = 5;
    mockWindows((_) {
      calls++;
      if (calls <= failures) throw MissingPluginException();
      return true;
    });

    expect(await RustDeskMultiWindowManager.invokeWindow(1, 'm', null), true);
    expect(calls, 6);

    calls = 0;
    failures = 1000;
    await expectLater(RustDeskMultiWindowManager.invokeWindow(1, 'm', null),
        throwsA(isA<MissingPluginException>()));
    expect(calls, 31);

    calls = 0;
    mockWindows((_) {
      calls++;
      throw PlatformException(code: 'error');
    });
    await expectLater(RustDeskMultiWindowManager.invokeWindow(1, 'm', null),
        throwsA(isA<PlatformException>()));
    expect(calls, 1);
  });

  test('newSession probe survives a window added while it waits', () async {
    final windows = [1, 2];
    // FileTransfer: openInTabs needs no FFI, and call() sees the manager's own empty list, so nothing else is invoked.
    final probed = <int>[];
    mockWindows((windowId) {
      probed.add(windowId);
      // A concurrent newSessionWindow registers a new window meanwhile; it has no handler yet, so it must not be probed.
      if (probed.length == 1) windows.add(3);
      return false;
    });

    await RustDeskMultiWindowManager.instance
        .newSession(WindowType.FileTransfer, 'm', 'id', windows);
    expect(probed, [1, 2]);
  });
}
