// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of dart.ui;

@pragma('vm:entry-point')
void _addView(
  int viewId,
  double devicePixelRatio,
  double width,
  double height,
  double viewPaddingTop,
  double viewPaddingRight,
  double viewPaddingBottom,
  double viewPaddingLeft,
  double viewInsetTop,
  double viewInsetRight,
  double viewInsetBottom,
  double viewInsetLeft,
  double systemGestureInsetTop,
  double systemGestureInsetRight,
  double systemGestureInsetBottom,
  double systemGestureInsetLeft,
  double physicalTouchSlop,
  List<double> displayFeaturesBounds,
  List<int> displayFeaturesType,
  List<int> displayFeaturesState,
  int displayId,
  double minWidth,
  double maxWidth,
  double minHeight,
  double maxHeight,
  double displayCornerRadiusTopLeft,
  double displayCornerRadiusTopRight,
  double displayCornerRadiusBottomRight,
  double displayCornerRadiusBottomLeft,
) {
  final _ViewConfiguration viewConfiguration = _buildViewConfiguration(
    devicePixelRatio,
    width,
    height,
    viewPaddingTop,
    viewPaddingRight,
    viewPaddingBottom,
    viewPaddingLeft,
    viewInsetTop,
    viewInsetRight,
    viewInsetBottom,
    viewInsetLeft,
    systemGestureInsetTop,
    systemGestureInsetRight,
    systemGestureInsetBottom,
    systemGestureInsetLeft,
    physicalTouchSlop,
    displayFeaturesBounds,
    displayFeaturesType,
    displayFeaturesState,
    displayId,
    minWidth,
    maxWidth,
    minHeight,
    maxHeight,
    displayCornerRadiusTopLeft,
    displayCornerRadiusTopRight,
    displayCornerRadiusBottomRight,
    displayCornerRadiusBottomLeft,
  );
  PlatformDispatcher.instance._addView(viewId, viewConfiguration);
}

@pragma('vm:entry-point')
void _removeView(int viewId) {
  PlatformDispatcher.instance._removeView(viewId);
}

@pragma('vm:entry-point')
void _sendViewFocusEvent(int viewId, int viewFocusState, int viewFocusDirection) {
  final viewFocusEvent = ViewFocusEvent(
    viewId: viewId,
    state: ViewFocusState.values[viewFocusState],
    direction: ViewFocusDirection.values[viewFocusDirection],
  );
  PlatformDispatcher.instance._sendViewFocusEvent(viewFocusEvent);
}

@pragma('vm:entry-point')
void _setEngineId(int engineId) {
  PlatformDispatcher.instance._engineId = engineId;
}

@pragma('vm:entry-point')
void _updateDisplays(
  List<int> ids,
  List<double> widths,
  List<double> heights,
  List<double> devicePixelRatios,
  List<double> refreshRates,
) {
  assert(ids.length == widths.length);
  assert(ids.length == heights.length);
  assert(ids.length == devicePixelRatios.length);
  assert(ids.length == refreshRates.length);
  final displays = <Display>[];
  for (var index = 0; index < ids.length; index += 1) {
    final int displayId = ids[index];
    displays.add(
      Display._(
        id: displayId,
        size: Size(widths[index], heights[index]),
        devicePixelRatio: devicePixelRatios[index],
        refreshRate: refreshRates[index],
      ),
    );
  }

  PlatformDispatcher.instance._updateDisplays(displays);
}

List<DisplayFeature> _decodeDisplayFeatures({
  required List<double> bounds,
  required List<int> type,
  required List<int> state,
  required double devicePixelRatio,
}) {
  assert(bounds.length / 4 == type.length, 'Bounds are rectangles, requiring 4 measurements each');
  assert(type.length == state.length);
  final result = <DisplayFeature>[];
  for (var i = 0; i < type.length; i++) {
    final int rectOffset = i * 4;
    result.add(
      DisplayFeature(
        bounds: Rect.fromLTRB(
          bounds[rectOffset] / devicePixelRatio,
          bounds[rectOffset + 1] / devicePixelRatio,
          bounds[rectOffset + 2] / devicePixelRatio,
          bounds[rectOffset + 3] / devicePixelRatio,
        ),
        type: DisplayFeatureType.values[type[i]],
        state: state[i] < DisplayFeatureState.values.length
            ? DisplayFeatureState.values[state[i]]
            : DisplayFeatureState.unknown,
      ),
    );
  }
  return result;
}

DisplayCornerRadii? _decodeDisplayCornerRadii({
  required double displayCornerRadiusTopLeft,
  required double displayCornerRadiusTopRight,
  required double displayCornerRadiusBottomRight,
  required double displayCornerRadiusBottomLeft,
}) {
  assert(() {
    final isTopLeftSet = displayCornerRadiusTopLeft != _kUnsetDisplayCornerRadius;
    final bool isConsistent =
        (displayCornerRadiusTopRight != _kUnsetDisplayCornerRadius) == isTopLeftSet &&
        (displayCornerRadiusBottomRight != _kUnsetDisplayCornerRadius) == isTopLeftSet &&
        (displayCornerRadiusBottomLeft != _kUnsetDisplayCornerRadius) == isTopLeftSet;

    if (!isConsistent) {
      throw ArgumentError(
        'The display corner radii must be either all set or all unset.\n'
        'Provided values were inconsistent:\n'
        '  TopLeft: $displayCornerRadiusTopLeft\n'
        '  TopRight: $displayCornerRadiusTopRight\n'
        '  BottomRight: $displayCornerRadiusBottomRight\n'
        '  BottomLeft: $displayCornerRadiusBottomLeft',
      );
    }
    return true;
  }());

  if (displayCornerRadiusTopLeft == _kUnsetDisplayCornerRadius ||
      displayCornerRadiusTopRight == _kUnsetDisplayCornerRadius ||
      displayCornerRadiusBottomRight == _kUnsetDisplayCornerRadius ||
      displayCornerRadiusBottomLeft == _kUnsetDisplayCornerRadius) {
    return null;
  }

  return DisplayCornerRadii(
    topLeft: displayCornerRadiusTopLeft,
    topRight: displayCornerRadiusTopRight,
    bottomRight: displayCornerRadiusBottomRight,
    bottomLeft: displayCornerRadiusBottomLeft,
  );
}

_ViewConfiguration _buildViewConfiguration(
  double devicePixelRatio,
  double width,
  double height,
  double viewPaddingTop,
  double viewPaddingRight,
  double viewPaddingBottom,
  double viewPaddingLeft,
  double viewInsetTop,
  double viewInsetRight,
  double viewInsetBottom,
  double viewInsetLeft,
  double systemGestureInsetTop,
  double systemGestureInsetRight,
  double systemGestureInsetBottom,
  double systemGestureInsetLeft,
  double physicalTouchSlop,
  List<double> displayFeaturesBounds,
  List<int> displayFeaturesType,
  List<int> displayFeaturesState,
  int displayId,
  double minWidth,
  double maxWidth,
  double minHeight,
  double maxHeight,
  double displayCornerRadiusTopLeft,
  double displayCornerRadiusTopRight,
  double displayCornerRadiusBottomRight,
  double displayCornerRadiusBottomLeft,
) {
  return _ViewConfiguration(
    devicePixelRatio: devicePixelRatio,
    size: Size(width, height),
    viewPadding: ViewPadding._(
      top: viewPaddingTop,
      right: viewPaddingRight,
      bottom: viewPaddingBottom,
      left: viewPaddingLeft,
    ),
    viewInsets: ViewPadding._(
      top: viewInsetTop,
      right: viewInsetRight,
      bottom: viewInsetBottom,
      left: viewInsetLeft,
    ),
    padding: ViewPadding._(
      top: math.max(0.0, viewPaddingTop - viewInsetTop),
      right: math.max(0.0, viewPaddingRight - viewInsetRight),
      bottom: math.max(0.0, viewPaddingBottom - viewInsetBottom),
      left: math.max(0.0, viewPaddingLeft - viewInsetLeft),
    ),
    systemGestureInsets: ViewPadding._(
      top: math.max(0.0, systemGestureInsetTop),
      right: math.max(0.0, systemGestureInsetRight),
      bottom: math.max(0.0, systemGestureInsetBottom),
      left: math.max(0.0, systemGestureInsetLeft),
    ),
    gestureSettings: GestureSettings(
      physicalTouchSlop: physicalTouchSlop == _kUnsetGestureSetting ? null : physicalTouchSlop,
    ),
    displayFeatures: _decodeDisplayFeatures(
      bounds: displayFeaturesBounds,
      type: displayFeaturesType,
      state: displayFeaturesState,
      devicePixelRatio: devicePixelRatio,
    ),
    displayId: displayId,
    viewConstraints: ViewConstraints(
      minWidth: minWidth,
      maxWidth: maxWidth,
      minHeight: minHeight,
      maxHeight: maxHeight,
    ),
    displayCornerRadii: _decodeDisplayCornerRadii(
      displayCornerRadiusTopLeft: displayCornerRadiusTopLeft,
      displayCornerRadiusTopRight: displayCornerRadiusTopRight,
      displayCornerRadiusBottomRight: displayCornerRadiusBottomRight,
      displayCornerRadiusBottomLeft: displayCornerRadiusBottomLeft,
    ),
  );
}

@pragma('vm:entry-point')
void _updateWindowMetrics(
  int viewId,
  double devicePixelRatio,
  double width,
  double height,
  double viewPaddingTop,
  double viewPaddingRight,
  double viewPaddingBottom,
  double viewPaddingLeft,
  double viewInsetTop,
  double viewInsetRight,
  double viewInsetBottom,
  double viewInsetLeft,
  double systemGestureInsetTop,
  double systemGestureInsetRight,
  double systemGestureInsetBottom,
  double systemGestureInsetLeft,
  double physicalTouchSlop,
  List<double> displayFeaturesBounds,
  List<int> displayFeaturesType,
  List<int> displayFeaturesState,
  int displayId,
  double minWidth,
  double maxWidth,
  double minHeight,
  double maxHeight,
  double displayCornerRadiusTopLeft,
  double displayCornerRadiusTopRight,
  double displayCornerRadiusBottomRight,
  double displayCornerRadiusBottomLeft,
) {
  final _ViewConfiguration viewConfiguration = _buildViewConfiguration(
    devicePixelRatio,
    width,
    height,
    viewPaddingTop,
    viewPaddingRight,
    viewPaddingBottom,
    viewPaddingLeft,
    viewInsetTop,
    viewInsetRight,
    viewInsetBottom,
    viewInsetLeft,
    systemGestureInsetTop,
    systemGestureInsetRight,
    systemGestureInsetBottom,
    systemGestureInsetLeft,
    physicalTouchSlop,
    displayFeaturesBounds,
    displayFeaturesType,
    displayFeaturesState,
    displayId,
    minWidth,
    maxWidth,
    minHeight,
    maxHeight,
    displayCornerRadiusTopLeft,
    displayCornerRadiusTopRight,
    displayCornerRadiusBottomRight,
    displayCornerRadiusBottomLeft,
  );
  PlatformDispatcher.instance._updateWindowMetrics(viewId, viewConfiguration);
}

typedef _LocaleClosure = String Function();

@pragma('vm:entry-point')
_LocaleClosure? _getLocaleClosure() => PlatformDispatcher.instance._localeClosure;

@pragma('vm:entry-point')
void _updateLocales(List<String> locales) {
  PlatformDispatcher.instance._updateLocales(locales);
}

@pragma('vm:entry-point')
void _updateUserSettingsData(String jsonData) {
  PlatformDispatcher.instance._updateUserSettingsData(jsonData);
}

@pragma('vm:entry-point')
void _updateInitialLifecycleState(String state) {
  PlatformDispatcher.instance._updateInitialLifecycleState(state);
}

@pragma('vm:entry-point')
void _updateSemanticsEnabled(bool enabled) {
  PlatformDispatcher.instance._updateSemanticsEnabled(enabled);
}

@pragma('vm:entry-point')
void _updateAccessibilityFeatures(int values) {
  PlatformDispatcher.instance._updateAccessibilityFeatures(values);
}

@pragma('vm:entry-point')
void _dispatchPlatformMessage(String name, ByteData? data, int responseId) {
  PlatformDispatcher.instance._dispatchPlatformMessage(name, data, responseId);
}

@pragma('vm:entry-point')
void _dispatchPointerDataPacket(ByteData packet) {
  PlatformDispatcher.instance._dispatchPointerDataPacket(packet);
}

@pragma('vm:entry-point')
class _HitTestResponse {
  _HitTestResponse({required this.hasPlatformView});

  @pragma('vm:entry-point')
  final bool hasPlatformView;
}

@pragma('vm:entry-point')
_HitTestResponse _hitTest(int viewId, double x, double y) {
  assert(PlatformDispatcher.instance._views.containsKey(viewId), 'View $viewId does not exist.');
  final FlutterView view = PlatformDispatcher.instance._views[viewId]!;
  final offset = Offset(x, y);
  final request = HitTestRequest(view: view, offset: offset);
  final HitTestResponse response = PlatformDispatcher.instance._hitTest(request);
  return _HitTestResponse(hasPlatformView: response.hasPlatformView);
}

@pragma('vm:entry-point')
void _dispatchSemanticsAction(int viewId, int nodeId, int action, ByteData? args) {
  PlatformDispatcher.instance._dispatchSemanticsAction(viewId, nodeId, action, args);
}

@pragma('vm:entry-point')
void _beginFrame(int microseconds, int frameNumber) {
  PlatformDispatcher.instance._beginFrame(microseconds);
  PlatformDispatcher.instance._updateFrameData(frameNumber);
}

@pragma('vm:entry-point')
void _reportTimings(List<int> timings) {
  PlatformDispatcher.instance._reportTimings(timings);
}

@pragma('vm:entry-point')
void _drawFrame() {
  // Route B (shorebird_route_b), second banking condition: the FIRST FRAMEWORK
  // FRAME.
  //
  // Named narrowly on purpose. This is the framework PRODUCING a frame, not
  // presentation, not pixels, and not a usable UI: rasterization happens
  // afterwards on the raster thread. Do not restate it as "the app booted".
  //
  // It is here and not in `Shell` because `Shell::OnAnimatorDraw` posts to the
  // raster task runner, so a reporter there would sit on another thread. Both
  // reporters stay at the Dart/root-isolate startup boundary.
  //
  // The updater's latch makes this idempotent; `_launchFrameReported` only
  // avoids an FFI call on every frame for the life of the process.
  if (_reportsLaunch && !_launchFrameReported) {
    _launchFrameReported = true;
    _reportLaunchSuccess();
  }
  PlatformDispatcher.instance._drawFrame();
}

bool _launchFrameReported = false;

/// Whether this engine has dart:ui report the launch outcome to the Shorebird
/// updater (a `shorebird_route_b` engine). A constant of the engine build,
/// asked once. When false, `_runMain` and `_drawFrame` behave exactly as they
/// do without Shorebird's Route B, because the Shell reports the launch.
final bool _reportsLaunch = _reportsLaunchFromDart();

@Native<Bool Function()>(
  symbol: 'PlatformConfigurationNativeApi::ReportsLaunchFromDart',
  isLeaf: true,
)
external bool _reportsLaunchFromDart();

/// Both outcomes feed ONE atomic latch in `shorebird::Updater`, which already
/// arbitrates success against failure: the first caller of either wins. That
/// implements `success = earliest(main completion, first framework frame)`
/// without new state, and it is also why a success banked at the first frame
/// permanently suppresses a later `main` failure. That is the accepted safety
/// trade-off, matching the updater's own asymmetry: the cheap error is to
/// retry, the expensive one is to tombstone a working patch.
@Native<Void Function()>(symbol: 'PlatformConfigurationNativeApi::ReportLaunchSuccess')
external void _reportLaunchSuccess();

@Native<Void Function()>(symbol: 'PlatformConfigurationNativeApi::ReportLaunchFailure')
external void _reportLaunchFailure();

@pragma('vm:entry-point')
bool _onError(Object error, StackTrace? stackTrace) {
  return PlatformDispatcher.instance._dispatchError(error, stackTrace ?? StackTrace.empty);
}

typedef _ListStringArgFunction = Object? Function(List<String> args);

@pragma('vm:entry-point')
void _runMain(Function startMainIsolateFunction, Function userMainFunction, List<String> args) {
  // ignore: avoid_dynamic_calls
  startMainIsolateFunction(() {
    if (!_reportsLaunch) {
      if (userMainFunction is _ListStringArgFunction) {
        userMainFunction(args);
      } else {
        userMainFunction(); // ignore: avoid_dynamic_calls
      }
      return;
    }
    // Route B, first banking condition: the completion of `main` ITSELF.
    //
    // `userMainFunction` is the handle the engine resolved for the app's
    // `main`, so a wrapper here is bound to it by construction rather than by
    // arrival order. Its return value was discarded twice above this point, so
    // moving the seam one level up loses the async signal entirely.
    //
    // Nothing is caught that the application already handles: an app whose
    // `main` wraps its work in `runZonedGuarded` returns normally here, which is
    // correct. Reaching below this boundary would report on application
    // business, not on whether the patch booted.
    final Object? result;
    try {
      if (userMainFunction is _ListStringArgFunction) {
        result = userMainFunction(args);
      } else {
        result = userMainFunction(); // ignore: avoid_dynamic_calls
      }
    } catch (_) {
      // A synchronous throw from `main` is a Dart-phase BOOT failure. Report it
      // and RETHROW unchanged: `PlatformDispatcher.onError` fires only for
      // UNHANDLED errors, so swallowing this would fix crash-backout by
      // deleting the application's own error reporting.
      _reportLaunchFailure();
      rethrow;
    }

    if (result is Future) {
      // `main` is async, and the Future is the ONLY completion signal: a
      // synchronous return from an `async` function means the body was merely
      // STARTED. `onError` forwards rather than consumes, for the same reason
      // the synchronous path rethrows.
      result.then<void>(
        (_) => _reportLaunchSuccess(),
        onError: (Object error, StackTrace stackTrace) {
          _reportLaunchFailure();
          Zone.current.handleUncaughtError(error, stackTrace);
        },
      );
      // NOT reported here. A `main` returning a Future that never completes is
      // neither a success nor a failure, it is still running, and treating "not
      // yet completed" as either would tombstone every slow boot. That shape
      // banks via the first framework frame instead; see `_drawFrame`.
    } else {
      _reportLaunchSuccess();
    }
  }, null);
}

/// Invokes [callback] inside the given [zone].
void _invoke(void Function()? callback, Zone zone) {
  if (callback == null) {
    return;
  }
  if (identical(zone, Zone.current)) {
    callback();
  } else {
    zone.runGuarded(callback);
  }
}

/// Invokes [callback] inside the given [zone] passing it [arg].
///
/// The 1 in the name refers to the number of arguments expected by
/// the callback (and thus passed to this function, in addition to the
/// callback itself and the zone in which the callback is executed).
void _invoke1<A>(void Function(A a)? callback, Zone zone, A arg) {
  if (callback == null) {
    return;
  }
  if (identical(zone, Zone.current)) {
    callback(arg);
  } else {
    zone.runUnaryGuarded<A>(callback, arg);
  }
}

/// Invokes [callback] inside the given [zone] passing it [arg1] and [arg2].
///
/// The 2 in the name refers to the number of arguments expected by
/// the callback (and thus passed to this function, in addition to the
/// callback itself and the zone in which the callback is executed).
void _invoke2<A1, A2>(void Function(A1 a1, A2 a2)? callback, Zone zone, A1 arg1, A2 arg2) {
  if (callback == null) {
    return;
  }
  if (identical(zone, Zone.current)) {
    callback(arg1, arg2);
  } else {
    zone.runGuarded(() {
      callback(arg1, arg2);
    });
  }
}

/// Invokes [callback] inside the given [zone] passing it [arg1], [arg2], and [arg3].
///
/// The 3 in the name refers to the number of arguments expected by
/// the callback (and thus passed to this function, in addition to the
/// callback itself and the zone in which the callback is executed).
void _invoke3<A1, A2, A3>(
  void Function(A1 a1, A2 a2, A3 a3)? callback,
  Zone zone,
  A1 arg1,
  A2 arg2,
  A3 arg3,
) {
  if (callback == null) {
    return;
  }
  if (identical(zone, Zone.current)) {
    callback(arg1, arg2, arg3);
  } else {
    zone.runGuarded(() {
      callback(arg1, arg2, arg3);
    });
  }
}

/// Invokes [callback] inside the given [zone] passing it [arg1],
/// and returns a nullable result of the specified type.
///
/// The 1 in the name refers to the number of arguments expected by
/// the callback (and thus passed to this function, in addition to the
/// callback itself and the zone in which the callback is executed).
R? _invoke1WithReturn<A1, R>(R Function(A1 a1)? callback, Zone zone, A1 arg1) {
  if (callback == null) {
    return null;
  }
  if (identical(zone, Zone.current)) {
    return callback(arg1);
  } else {
    return runZonedGuarded(
      () {
        return callback(arg1);
      },
      (e, s) {
        zone.handleUncaughtError(e, s);
      },
    );
  }
}

bool _isLoopback(String host) {
  if (host.isEmpty) {
    return false;
  }
  if ('localhost' == host) {
    return true;
  }
  try {
    return InternetAddress(host).isLoopback;
  } on ArgumentError {
    return false;
  }
}

/// Loopback connections are always allowed.
/// Zone override with 'flutter.io.allow_http' takes first priority.
/// If zone override is not provided, engine setting is checked.
@pragma('vm:entry-point')
void Function(Uri) _getHttpConnectionHookClosure(bool mayInsecurelyConnectToAllDomains) {
  return (Uri uri) {
    final Object? zoneOverride = Zone.current[#flutter.io.allow_http];
    if (zoneOverride == true) {
      return;
    }
    if (zoneOverride == false && uri.isScheme('http')) {
      // Going to _isLoopback check before throwing
    } else if (mayInsecurelyConnectToAllDomains || uri.isScheme('https')) {
      // In absence of zone override, if engine setting allows the connection
      // or if connection is to `https`, allow the connection.
      return;
    }
    // Loopback connections are always allowed
    // Check at last resort to avoid debug annoyance of try/on ArgumentError
    if (_isLoopback(uri.host)) {
      return;
    }
    throw UnsupportedError(
      'Non-https connection "$uri" is not supported by the platform. '
      'Refer to https://docs.flutter.dev/release/breaking-changes/network-policy-ios-android.',
    );
  };
}
