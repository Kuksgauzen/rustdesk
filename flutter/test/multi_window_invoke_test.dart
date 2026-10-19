import 'package:flutter/services.dart';
import 'package:flutter_hbb/utils/multi_window_manager.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('invokeWindow waits for a window that has no handler yet', () async {
    RustDeskMultiWindowManager.invokeRetryDelay = Duration.zero;
    var calls = 0;
    var failures = 5;
    RustDeskMultiWindowManager.invoke = (windowId, method, args) async {
      calls++;
      if (calls <= failures) {
        throw MissingPluginException();
      }
      return true;
    };

    expect(await RustDeskMultiWindowManager.invokeWindow(1, 'm', null), true);
    expect(calls, 6);

    calls = 0;
    failures = 1000;
    await expectLater(RustDeskMultiWindowManager.invokeWindow(1, 'm', null),
        throwsA(isA<MissingPluginException>()));
    expect(calls, 31);

    calls = 0;
    RustDeskMultiWindowManager.invoke = (windowId, method, args) async {
      calls++;
      throw PlatformException(code: 'error');
    };
    await expectLater(RustDeskMultiWindowManager.invokeWindow(1, 'm', null),
        throwsA(isA<PlatformException>()));
    expect(calls, 1);
  });

  test('newSession probe survives a window added while it waits', () async {
    final windows = [1, 2];
    // FileTransfer: openInTabs needs no FFI, and call() sees the manager's own empty list, so nothing else is invoked.
    final probed = <int>[];
    RustDeskMultiWindowManager.invoke = (windowId, method, args) async {
      probed.add(windowId);
      // A concurrent newSessionWindow registers a new window meanwhile; it has no handler yet, so it must not be probed.
      if (probed.length == 1) windows.add(3);
      return false;
    };

    await RustDeskMultiWindowManager.instance
        .newSession(WindowType.FileTransfer, 'm', 'id', windows);
    expect(probed, [1, 2]);
  });
}
