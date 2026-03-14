from PySide6.QtCore import QSize, Qt, QRect, QTimer
from PySide6.QtWidgets import QApplication, QMainWindow, QProgressBar, QPushButton, QVBoxLayout, QHBoxLayout, QLabel, QWidget, QMessageBox
from PySide6.QtGui import QPixmap, QPainter, QImage, QColorConstants, QColor
import sys
from functools import partial
import serial, serial.tools.list_ports

DEVICE_VID = 0xCafe
DEVICE_PID = 0x4005

HANDBRAKE_CALIBRATION_DEADZONE = 0.03

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()

        self.setWindowTitle("SimHShifter-Configurator")

        self.open_serial_port()

        self.gear_position = {}
        for gear in ["1", "2", "3", "4", "5", "6", "R", "N"]:
            position = self.serial_exec_command("get " + gear).split(" ")
            self.gear_position[gear] = [int(position[0]), int(position[1])]

        # TODO: dobi min, max handbrake output
        self.calibrating_handbrake = False

        layoutH = QHBoxLayout()
        layoutH.addWidget(self.create_gear_position_display())
        layoutH.addLayout(self.create_gear_table_layout())

        layoutV = QVBoxLayout()
        layoutV.addLayout(layoutH)
        layoutV.addWidget(self.create_handbrake_layout())

        widget = QWidget()
        widget.setLayout(layoutV)
        self.setCentralWidget(widget)

        self.setFixedSize(self.minimumSize())

        update_timer = QTimer(self)
        update_timer.timeout.connect(self.update_gear_display)
        update_timer.timeout.connect(self.update_handbrake_bars)
        update_timer.start(50)
    
    def create_gear_table_layout(self):
        layout = QVBoxLayout()

        self.position_labels = {}
        
        for gear in self.gear_position:
            layout2 = QHBoxLayout()
            layout2.addWidget(QLabel("<b>" + gear + "</b>"))

            label_x = QLabel(f"x: {self.gear_position[gear][0]}")
            label_y = QLabel(f"y: {self.gear_position[gear][1]}")
            self.position_labels[gear] = [label_x, label_y]
            layout2.addWidget(label_x)
            layout2.addWidget(label_y)

            button = QPushButton("Calibrate")
            button.clicked.connect(partial(self.calibrate_clicked, gear))
            layout2.addWidget(button)

            layout.addLayout(layout2)

        flash_button = QPushButton("Write to Flash")
        flash_button.clicked.connect(self.write_flash_clicked)
        layout.addWidget(flash_button)

        read_flash_button = QPushButton("Read from Flash")
        read_flash_button.clicked.connect(self.read_flash_clicked)
        layout.addWidget(read_flash_button)

        layout.setContentsMargins(10, 10, 10, 10)
        return layout

    def create_handbrake_layout(self):
        layout = QVBoxLayout()
        layout.setSpacing(0)

        layoutH = QHBoxLayout()
        layoutH.setSpacing(10)
        layoutH.addWidget(QLabel("<b>Handbrake:</b>"))
        calibrate_button = QPushButton("Calibrate")
        calibrate_button.clicked.connect(partial(self.calibrate_handbrake_clicked, calibrate_button))

        layoutH.addWidget(calibrate_button)
        layoutH.addWidget(QLabel(f"(calibration deadzone: {HANDBRAKE_CALIBRATION_DEADZONE*100}%)"))
        layoutH.addStretch(1)

        layout.addLayout(layoutH)
        layout.addSpacing(5)

        self.handbrake_bar_raw, bar_raw_layout = self.create_handbrake_progress_bar("raw:")
        layout.addLayout(bar_raw_layout)

        self.handbrake_bar_out, bar_out_layout = self.create_handbrake_progress_bar("out:")
        self.handbrake_bar_out.setStyleSheet("QProgressBar::chunk { background-color: #ff5c5c; }")
        layout.addLayout(bar_out_layout)

        widget = QWidget()
        widget.setLayout(layout)
        widget.setStyleSheet("""
            QProgressBar {
                background-color: #EEE;
            }
            QProgressBar::chunk {
                background-color: #4CAF50; /* The fill color */
                width: 1px; /* Makes the fill look smooth */
            }
        """)
        return widget

    def create_handbrake_progress_bar(self, text):
        layout = QHBoxLayout()
        label = QLabel(text)
        label.setFixedWidth(30)
        layout.addWidget(label)

        bar = QProgressBar()
        bar.setRange(0, 4095)
        bar.setValue(0)
        bar.setAlignment(Qt.AlignCenter)
        bar.setFormat(f"%v/{bar.maximum()} (%p%)")
        layout.addWidget(bar)

        return bar, layout

    def calibrate_handbrake_clicked(self, calibrate_button):
        calibrate_button.setText("Calibrate" if self.calibrating_handbrake else "Stop calib.")
        self.calibrating_handbrake = not self.calibrating_handbrake

    def update_handbrake_bars(self):
        # TODO: if calibrating: update min, max (also using deadzone)
        # update bars
        return

    def create_gear_position_display(self):
        self.canvas = QImage(400, 300, QImage.Format_RGB32)
        self.canvas_label = QLabel()
        self.update_gear_display()
        return self.canvas_label

    def update_gear_display(self):
        if self.isMinimized():
            return
        painter = QPainter(self.canvas)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.fillRect(self.canvas.rect(), QColor(225, 225, 225))

        min_x = min(pos[0] for pos in self.gear_position.values())
        min_y = min(-pos[1] for pos in self.gear_position.values())
        max_x = max(pos[0] for pos in self.gear_position.values())
        max_y = max(-pos[1] for pos in self.gear_position.values())

        if max_x == min_x:
            max_x += 1
        if max_y == min_y:
            max_y += 1

        for gear in self.gear_position:
            pos_x = (self.gear_position[gear][0] - min_x) / (max_x - min_x) * 300 + 50 - 16
            pos_y = (-self.gear_position[gear][1] - min_y) / (max_y - min_y) * 200 + 50 - 16
            painter.drawText(QRect(pos_x, pos_y, 32, 32), Qt.AlignCenter, gear)
            painter.drawArc(pos_x, pos_y, 32, 32, 0, 16 * 360)

        position = self.serial_exec_command("get pos", debug=False).split(" ")
        self.current_pos = [int(position[0]), int(position[1])]
        pos_x = (self.current_pos[0] - min_x) / (max_x - min_x) * 300 + 50 - 16
        pos_y = (-self.current_pos[1] - min_y) / (max_y - min_y) * 200 + 50 - 16
        painter.drawText(QRect(pos_x, pos_y, 32, 32), Qt.AlignCenter, "+")
        painter.drawArc(pos_x, pos_y, 32, 32, 0, 16 * 360)

        painter.end()
        self.canvas_label.setPixmap(QPixmap.fromImage(self.canvas))

    def calibrate_clicked(self, gear):
        self.serial_exec_command("set " + gear, "ok")
        position = self.serial_exec_command("get " + gear).split(" ")
        self.gear_position[gear] = [int(position[0]), int(position[1])]
        self.position_labels[gear][0].setText(f"x: {position[0]}")
        self.position_labels[gear][1].setText(f"y: {position[1]}")

    def write_flash_clicked(self):
        out = self.serial_exec_command("flash write", "ok")
        if out == "ok":
            dialog = QMessageBox()
            dialog.setWindowTitle("Success")
            dialog.setIcon(QMessageBox.Information)
            dialog.setText("Write to flash ok")
            dialog.exec()

    def read_flash_clicked(self):
        out = self.serial_exec_command("flash read", "ok")
        if out != "ok":
            return

        for gear in self.gear_position:
            position = self.serial_exec_command("get " + gear).split(" ")
            self.gear_position[gear] = [int(position[0]), int(position[1])]
            self.position_labels[gear][0].setText(f"x: {position[0]}")
            self.position_labels[gear][1].setText(f"y: {position[1]}")

        dialog = QMessageBox()
        dialog.setWindowTitle("Success")
        dialog.setIcon(QMessageBox.Information)
        dialog.setText("Read from flash ok")
        dialog.exec()

    def open_serial_port(self):
        port_list = list(serial.tools.list_ports.comports())
        found_str = ""
        for port_info in port_list:
            port_info_str = f"found device: {port_info.device} - {port_info.description} - {port_info.hwid}"
            print(port_info_str)
            found_str += port_info_str + "\n\n"

        selected_port = None
        for port_info in port_list:
            if port_info.vid == DEVICE_VID and port_info.pid == DEVICE_PID:
                print("selected", port_info.device)
                selected_port = port_info.device
                break
        
        if selected_port == None:
            dialog = QMessageBox()
            dialog.setWindowTitle("Error")
            dialog.setIcon(QMessageBox.Critical)
            dialog.setText(found_str + f"ERROR: Failed to find a port with device USB VID:PID={hex(DEVICE_VID)[2:]}:{hex(DEVICE_PID)[2:]}")
            dialog.exec()
            sys.exit(1)
        
        self.serial = serial.Serial(selected_port, 9600, 8, "N", timeout=1)
        self.serial_exec_command("test", "ok")

    def serial_exec_command(self, cmd: str, expected="", debug=True):
        cmd = cmd.encode()
        expected = expected.encode()
        self.serial.write(cmd + b"\n")
        _echo = self.serial.readline().strip()
        out = self.serial.readline().strip()
        if debug:
            print(f"executed: {cmd.ljust(16)} - received: {out}")
        if (expected != b"" and out != expected) or len(out) == 0:
            dialog = QMessageBox()
            dialog.setWindowTitle("Error")
            dialog.setIcon(QMessageBox.Critical)
            dialog.setText(f"ERROR: Failed to execute serial command {cmd}\nresult: {out}\nexpected: {expected}")
            dialog.exec()
        return out.decode()


if __name__ == "__main__":
    app = QApplication(sys.argv)

    window = MainWindow()
    window.show()

    app.exec()