import 'dart:async';

import 'package:flutter/material.dart';
import 'package:web_socket_channel/io.dart';

void main() {
  runApp(const SmartLEDApp());
}

class SmartLEDApp extends StatelessWidget {
  const SmartLEDApp({super.key});

  @override
  Widget build(BuildContext context) {
    return const MaterialApp(
      title: 'Smart LED',
      home: SmartLEDHomePage(),
    );
  }
}

class SmartLEDHomePage extends StatefulWidget {
  const SmartLEDHomePage({super.key});

  @override
  State<SmartLEDHomePage> createState() => _SmartLEDHomePageState();
}

class _SmartLEDHomePageState extends State<SmartLEDHomePage> {
  final List<String> _messages = [];
  final ScrollController _scrollController = ScrollController();
  final TextEditingController _textEditingController =
      TextEditingController(text: "ws://");
  late IOWebSocketChannel _channel;
  bool _connected = false;
  bool _ledOn = false;

  Future<String?> getAddress(BuildContext context) {
    return showDialog<String>(
        context: context,
        barrierDismissible: false,
        builder: (context) => AlertDialog(
                title: const Text('Enter Smart LED server address'),
                content: TextField(
                  controller: _textEditingController,
                  keyboardType: TextInputType.url,
                ),
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
    _scrollController.animateTo(
      _scrollController.position.maxScrollExtent + 16,
      duration: const Duration(milliseconds: 500),
      curve: Curves.easeOut,
    );
  }

  void connect(BuildContext context) async {
    String? address = await getAddress(context);
    if (address == null) {
      return;
    }
    log("Connecting to $address.");
    try {
      _channel = IOWebSocketChannel.connect(address);
      _channel.stream.listen(onData, onError: onError);
      log("Connected succesfully.");
      _connected = true;
    } catch (e) {
      log("Connect failed:");
      log(e.toString());
    }
  }

  void onData(dynamic message) {
    List<int> data = message;
    if (data.length == 1) {
      setState(() {
        _ledOn = data[0] != 0;
      });
      String state = _ledOn ? "on" : "off";
      log("Received LED state: $state.");
    } else {
      log("Received invalid message.");
    }
  }

  void onError(Object error) {
    log("Channel error:");
    log(error.toString());
    _connected = false;
  }

  void onLEDTap(BuildContext context) async {
    if (_connected) {
      String request = _ledOn ? "off" : "on";
      log("Sending request to turn LED $request.");
      _channel.sink.add([_ledOn ? 0 : 1]);
    } else {
      connect(context);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
        backgroundColor: _ledOn
            ? const Color.fromARGB(222, 255, 255, 255)
            : const Color.fromARGB(255, 12, 12, 12),
        body: GestureDetector(
          onTap: () {
            onLEDTap(context);
          },
          child: CustomPaint(
              foregroundPainter: LEDPainter(_ledOn),
              child: ListView.builder(
                  controller: _scrollController,
                  padding: const EdgeInsets.all(8.0),
                  itemCount: _messages.length,
                  itemBuilder: ((context, index) {
                    return Text(_messages[index],
                        style: TextStyle(
                            color: _ledOn ? Colors.black : Colors.green,
                            fontWeight: FontWeight.bold));
                  }))),
        ));
  }

  @override
  void dispose() {
    _channel.sink.close();
    _scrollController.dispose();
    _textEditingController.dispose();
    super.dispose();
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
