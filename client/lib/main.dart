import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:web_socket_channel/io.dart';

import 'package:web_socket_channel/web_socket_channel.dart';
import 'package:after_layout/after_layout.dart';

void main() {
  runApp(const SmartLEDApp());
}

class SmartLEDApp extends StatefulWidget {
  static final navKey = GlobalKey<NavigatorState>();
  const SmartLEDApp({super.key});

  @override
  State<SmartLEDApp> createState() => _SmartLEDAppState();
}

class _SmartLEDAppState extends State<SmartLEDApp> {
  final List<String> _messages = [];
  final ScrollController _controller = ScrollController();
  final TextEditingController _textEditingController = TextEditingController();
  bool on = false;
  late IOWebSocketChannel _channel;
  bool connected = false;

  Future<String?> getAddress() {
    return showDialog<String>(
        context: SmartLEDApp.navKey.currentState!.overlay!.context,
        barrierDismissible: false,
        builder: (context) => AlertDialog(
                title: const Text('Smart LED websocket server address'),
                content: TextField(
                    controller: _textEditingController,
                    decoration:
                        const InputDecoration(hintText: 'ws://127.0.0.1')),
                actions: [
                  TextButton(
                    onPressed: () =>
                        Navigator.pop(context, _textEditingController.text),
                    child: const Text('OK'),
                  )
                ]));
  }

  void log(String message) {
    String timestamp = DateTime.now().toIso8601String();
    setState(() {
      _messages.add("$timestamp: $message");
    });
    _controller.animateTo(
      _controller.position.maxScrollExtent + 16,
      duration: const Duration(milliseconds: 500),
      curve: Curves.easeOut,
    );
  }

  void connect() async {
    String? address = await getAddress();
    if (address == null) {
      return;
    }
    log("Connecting to $address.");
    try {
      _channel = IOWebSocketChannel.connect(address);
      _channel.stream.listen(onData, onError: onError);
      log("Connected succesfully.");
      connected = true;
    } catch (e) {
      log("Connect failed:");
      log(e.toString());
    }
  }

  void onData(dynamic message) {
    log("Received message:");
    log(message.toString());
    // dynamic decodedMessage = jsonDecode(message);
  }

  void onError(Object error) {
    log("Channel error:");
    log(error.toString());
    connected = false;
  }

  void onLEDTap() async {
    if (connected) {
      String request = on ? "off" : "on";
      log("Sending request to turn LED $request.");
      _channel.sink.add(jsonEncode({
        'state': request,
      }));
    } else {
      connect();
    }
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      navigatorKey: SmartLEDApp.navKey,
      title: 'Smart LED',
      theme: ThemeData(
        primarySwatch: Colors.blue,
      ),
      home: Scaffold(
          backgroundColor: on
              ? const Color.fromARGB(222, 255, 255, 255)
              : const Color.fromARGB(255, 12, 12, 12),
          body: GestureDetector(
            onTap: onLEDTap,
            child: CustomPaint(
                foregroundPainter: LEDPainter(on),
                child: ListView.builder(
                    controller: _controller,
                    padding: const EdgeInsets.all(8.0),
                    itemCount: _messages.length,
                    itemBuilder: ((context, index) {
                      return Text(_messages[index],
                          style: TextStyle(
                              color: on ? Colors.black : Colors.green,
                              fontWeight: FontWeight.bold));
                    }))),
          )),
    );
  }
}

class LEDPainter extends CustomPainter {
  LEDPainter(this.on);

  final bool on;

  @override
  void paint(Canvas canvas, Size size) {
    final double unit = size.height / 10.0;
    const double ringWidthMultiplier = 1.2;
    const double ringHeightMultiplier = 1.4;
    const double strokeMultiplier = 0.15;
    const double legMultiplier = 0.6;

    Path path = Path();
    path.moveTo(size.width / 2 - unit, size.height / 2 - unit);
    path.arcToPoint(Offset(size.width / 2 + unit, size.height / 2 - unit),
        radius: Radius.circular(unit), rotation: 180.0);
    path.lineTo(size.width / 2 + unit, size.height / 2 + unit);
    path.lineTo(
        size.width / 2 + ringWidthMultiplier * unit, size.height / 2 + unit);
    path.lineTo(size.width / 2 + ringWidthMultiplier * unit,
        size.height / 2 + ringHeightMultiplier * unit);
    path.lineTo(size.width / 2 - ringWidthMultiplier * unit,
        size.height / 2 + ringHeightMultiplier * unit);
    path.lineTo(
        size.width / 2 - ringWidthMultiplier * unit, size.height / 2 + unit);
    path.lineTo(size.width / 2 - unit, size.height / 2 + unit);
    path.close();

    Paint paint = Paint();
    paint.color = on ? Colors.red : Colors.red.withAlpha(25);
    paint.style = PaintingStyle.fill;
    canvas.drawPath(path, paint);

    paint.color = Colors.black;
    paint.style = PaintingStyle.stroke;
    paint.strokeWidth = strokeMultiplier * unit;
    canvas.drawPath(path, paint);

    canvas.drawLine(
        Offset(size.width / 2 - legMultiplier * unit,
            size.height / 2 + ringHeightMultiplier * unit),
        Offset(size.width / 2 - legMultiplier * unit, size.height),
        paint);
    canvas.drawLine(
        Offset(size.width / 2 + legMultiplier * unit,
            size.height / 2 + ringHeightMultiplier * unit),
        Offset(size.width / 2 + legMultiplier * unit, size.height),
        paint);
  }

  @override
  bool shouldRepaint(LEDPainter oldDelegate) {
    return oldDelegate.on != on;
  }
}
