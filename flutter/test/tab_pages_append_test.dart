import 'package:flutter/widgets.dart';
import 'package:flutter_hbb/desktop/widgets/tabbar_widget.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  // A burst of links can add several tabs before the next build. The pages
  // already built must stay in the same list, or they are disposed.
  test('several tabs added at once keep the shown list', () {
    final a = Container(), b = Container(), c = Container(), d = Container();
    final shown = <Widget>[a];
    expect(identical(appendPages(shown, [a, b, c, d]), shown), isTrue);
    expect(shown, [a, b, c, d]);
    // A window shown again builds its pages anew.
    expect(appendPages(<Widget>[], [a, b]), isNull);
  });
}
