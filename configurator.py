from PySide6.QtCore import QSize, Qt, QRect
from PySide6.QtWidgets import QApplication, QMainWindow, QPushButton, QVBoxLayout, QHBoxLayout, QLabel, QWidget, QMessageBox
from PySide6.QtGui import QPixmap, QPainter, QImage, QColorConstants, QColor
import sys
from functools import partial
import serial, serial.tools.list_ports

DEVICE_VID = 0xCafe
DEVICE_PID = 0x4005

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()

        self.setWindowTitle("SimHShifter-Configurator")

        layout = QHBoxLayout()
        layout.addWidget(self.create_gear_position_display())
        layout.addLayout(self.create_gear_table_layout())

        widget = QWidget()
        widget.setLayout(layout)
        self.setCentralWidget(widget)
    
    def create_gear_table_layout(self):
        layout = QVBoxLayout()
        
        for gear in ["1", "2", "3", "4", "5", "6", "R"]:
            layout2 = QHBoxLayout()
            layout2.addWidget(QLabel(gear))
            layout2.addWidget(QLabel("x: 500"))
            layout2.addWidget(QLabel("y: 2000"))

            button = QPushButton("Calibrate")
            button.clicked.connect(partial(self.calibrate_clicked, gear))
            layout2.addWidget(button)

            layout.addLayout(layout2)

        layout.setContentsMargins(10, 10, 10, 10)
        return layout

    def create_gear_position_display(self):
        canvas = QImage(400, 300, QImage.Format_ARGB32)
        #canvas.fill(QColorConstants.Transparent)
        canvas.fill(QColor(210, 210, 210))
        painter = QPainter(canvas)
        painter.setRenderHint(QPainter.Antialiasing)

        for gear, pos_x, pos_y in [("1", 50, 50), ("2", 50, 250), ("3", 100, 50), ("4", 100, 250), ("5", 150, 50), ("6", 150, 250), ("R", 16, 50)]:
            painter.drawPoint(pos_x, pos_y)
            pos_x -= 16
            pos_y -= 16
            painter.drawText(QRect(pos_x, pos_y, 32, 32), Qt.AlignCenter, gear)
            painter.drawArc(pos_x, pos_y, 32, 32, 0, 16 * 360)
        painter.end()

        label = QLabel()
        label.setPixmap(QPixmap.fromImage(canvas))
        return label
    
    def calibrate_clicked(self, gear):
        print("clicked", gear)
    
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
        self.serial.write(b"test\n")
        print("read", self.serial.readline().decode("utf-8"))
        print("read", self.serial.readline().decode("utf-8"))


if __name__ == "__main__":
    app = QApplication(sys.argv)

    window = MainWindow()
    window.show()

    window.open_serial_port()

    app.exec()