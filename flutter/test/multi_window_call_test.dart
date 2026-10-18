import 'dart:async';

import 'package:desktop_multi_window/desktop_multi_window.dart';
import 'package:flutter/services.dart';
import 'package:flutter_hbb/consts.dart';
import 'package:flutter_hbb/utils/multi_window_manager.dart';
import 'package:flutter_test/flutter_test.dart';

const _channel = MethodChannel('mixin.one/flutter_multi_window_channel');

void main() {
  final binding = TestWidgetsFlutterBinding.ensureInitialized();

  tearDown(() =>
      binding.defaultBinaryMessenger.setMockMethodCallHandler(_channel, null));

  test('a session sent while the window starts reaches it once', () async {
    // The sub window sets its method handler after 3 calls; until then the
    // plugin answers as for a channel without a handler.
    final served = <String>[];
    var calls = 0;
    binding.defaultBinaryMessenger.setMockMethodCallHandler(_channel,
        (call) async {
      if (++calls <= 3) {
        throw MissingPluginException();
      }
      served.add((call.arguments as Map)['arguments'] as String);
      return true;
    });
    expect(
        await callWhileWindowStarts(
            () => DesktopMultiWindow.invokeMethod(
                1, kWindowEventNewRemoteDesktop, 'u2'),
            interval: Duration.zero),
        isTrue);
    expect(served, ['u2']);
  });

  test('sessions that come together open as tabs of one window', () async {
    // A tab window that appears in [windows] some time after it is asked for,
    // like [RustDeskMultiWindowManager.newSessionWindow].
    final windows = <int>[], created = <String>[], sent = <String>[];
    final creating = <Object, Future<int>>{};
    Future<MultiWindowCallResult> open(String id) =>
        openInTabWindow(creating, 'remote', windows, () async {
          created.add(id);
          await Future.delayed(const Duration(milliseconds: 10));
          windows.add(created.length);
          return created.length;
        }, () async {
          sent.add(id);
          return MultiWindowCallResult(windows.first, true);
        });

    final results = await Future.wait(['u1', 'u2', 'u3'].map(open));
    expect(created, ['u1']);
    expect(sent, ['u2', 'u3']);
    expect(results.map((r) => r.windowId), [1, 1, 1]);
    expect(creating, isEmpty);
  });

  test('a new window is waited for until it answers', () async {
    // Before its engine runs, the window does not answer; then it has no
    // method handler for a while.
    var calls = 0;
    var answered = false;
    await waitWindowReady(() {
      calls++;
      if (calls <= 2) return Completer<dynamic>().future;
      if (calls <= 5) throw MissingPluginException();
      answered = true;
      return Future.value('');
    }, interval: const Duration(milliseconds: 1));
    expect(answered, isTrue);
  });
}
