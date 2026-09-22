"""
TraceQ Arduino Configurator
============================
Arduino 기기 설정 읽기 / 쓰기 / 날짜동기화 / 태그 초기화

프로토콜:
  최신 모드(IsLatestCompat=True) : STX{json}ETX  ↔  STX{json}ETX
  레거시 모드(IsLatestCompat=False): PSOk → Z 인증 후 S/M/C 명령
  (Configurator는 JSON 모드 전용 — 레거시에서도 JSON 명령은 수신됨)
"""
from __future__ import annotations
import base64
import json
import sys
import threading
import time
from datetime import datetime
from typing import Optional

from PyQt6.QtCore import Qt, QTimer, pyqtSignal, QObject
from PyQt6.QtGui import QFont, QColor, QPalette, QTextCharFormat
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget,
    QVBoxLayout, QHBoxLayout, QGridLayout,
    QGroupBox, QLabel, QComboBox, QPushButton,
    QSpinBox, QCheckBox, QTextEdit, QLineEdit,
    QDateTimeEdit, QFrame, QMessageBox, QSizePolicy,
    QScrollArea,
)
from PyQt6.QtCore import QDateTime

# ─── 프로토콜 상수 ────────────────────────────────────────
STX = 0x02
ETX = 0x03
BAUD  = 115200
BUF_MAX = 8192


def find_arduino_port() -> Optional[str]:
    """CH340 / CH341 / Arduino 키워드로 포트 자동 감지."""
    try:
        import serial.tools.list_ports
        for p in serial.tools.list_ports.comports():
            desc = (p.description  or '').lower()
            mfr  = (p.manufacturer or '').lower()
            if ('arduino' in desc or 'arduino' in mfr
                    or 'ch340' in desc or 'ch341' in desc
                    or 'ch340' in mfr  or 'ch341' in mfr):
                return p.device
    except Exception:
        pass
    return None


DEVICE_TYPES = [
    ('S', '서버 (Server)'),
    ('G', '게이트웨이 (Gateway)'),
    ('W', '세척기 (Washing)'),
    ('D', '소독기 (Disinfection)'),
]

# ─── 스타일 ───────────────────────────────────────────────
_DARK = """
QMainWindow, QWidget { background:#2b2b2b; color:#ddd; }
QGroupBox {
    border:1px solid #555; border-radius:4px;
    margin-top:8px; padding-top:4px;
    font-weight:bold; color:#aaa;
}
QGroupBox::title { subcontrol-origin:margin; left:8px; padding:0 4px; }
QLabel { color:#ccc; }
QComboBox, QSpinBox, QLineEdit, QDateTimeEdit {
    background:#3c3c3c; color:#ddd; border:1px solid #555;
    border-radius:3px; padding:3px 6px; min-height:22px;
}
QComboBox::drop-down { border:none; }
QComboBox QAbstractItemView { background:#3c3c3c; color:#ddd; selection-background-color:#555; }
QPushButton {
    background:#4a4a4a; color:#ddd; border:1px solid #666;
    border-radius:3px; padding:5px 14px; min-height:26px;
}
QPushButton:hover  { background:#5a5a5a; }
QPushButton:pressed { background:#3a3a3a; }
QPushButton:disabled { background:#333; color:#666; border-color:#444; }
QCheckBox { color:#ccc; spacing:6px; }
QCheckBox::indicator { width:14px; height:14px;
    border:1px solid #666; border-radius:2px; background:#3c3c3c; }
QCheckBox::indicator:checked { background:#4a9eff; border-color:#4a9eff; }
QTextEdit {
    background:#1e1e1e; color:#b0d0b0; border:1px solid #444;
    border-radius:3px; font-family:Consolas,monospace; font-size:11px;
}
QScrollBar:vertical { background:#2b2b2b; width:10px; }
QScrollBar::handle:vertical { background:#555; border-radius:5px; min-height:20px; }
"""

_BTN_GREEN  = "QPushButton{background:#2d6a2d;color:#ddd;border:1px solid #4a9a4a;border-radius:3px;padding:5px 14px;min-height:26px;} QPushButton:hover{background:#3d8a3d;} QPushButton:disabled{background:#333;color:#666;}"
_BTN_BLUE   = "QPushButton{background:#1e4a8a;color:#ddd;border:1px solid #3a7acc;border-radius:3px;padding:5px 14px;min-height:26px;} QPushButton:hover{background:#2a5a9a;} QPushButton:disabled{background:#333;color:#666;}"
_BTN_ORANGE = "QPushButton{background:#7a4a00;color:#ddd;border:1px solid #cc8800;border-radius:3px;padding:5px 14px;min-height:26px;} QPushButton:hover{background:#8a5a10;} QPushButton:disabled{background:#333;color:#666;}"
_BTN_RED    = "QPushButton{background:#7a1a1a;color:#ddd;border:1px solid #cc3333;border-radius:3px;padding:5px 14px;min-height:26px;} QPushButton:hover{background:#9a2a2a;} QPushButton:disabled{background:#333;color:#666;}"
_BTN_PURPLE = "QPushButton{background:#3a2a6a;color:#ddd;border:1px solid #7755cc;border-radius:3px;padding:5px 14px;min-height:26px;} QPushButton:hover{background:#4a3a8a;} QPushButton:disabled{background:#333;color:#666;}"


# ═══════════════════════════════════════════════════════════
# 시리얼 통신 워커
# ═══════════════════════════════════════════════════════════
class SerialWorker(QObject):
    """백그라운드 시리얼 읽기 + 메시지 파싱"""
    json_received = pyqtSignal(dict)   # JSON 응답
    line_received = pyqtSignal(str)    # 일반 텍스트 라인
    connected     = pyqtSignal(str)    # 연결 성공 → 포트명
    disconnected  = pyqtSignal()       # 연결 해제

    def __init__(self, parent=None):
        super().__init__(parent)
        self._ser: Optional[object] = None
        self._buf = bytearray()
        self._lock = threading.Lock()
        self._running = False
        self._thread: Optional[threading.Thread] = None

    # ── 연결 ─────────────────────────────────────────────
    def connect(self, port: str) -> bool:
        try:
            import serial as _s
            ser = _s.Serial(port, BAUD,
                            bytesize=_s.EIGHTBITS,
                            parity=_s.PARITY_NONE,
                            stopbits=_s.STOPBITS_ONE,
                            timeout=0.1)
            # DTR 토글 → Arduino 리셋
            ser.dtr = False; time.sleep(0.1)
            ser.dtr = True;  time.sleep(2.0)
            ser.reset_input_buffer()
            self._ser = ser
            self._buf.clear()
            self._running = True
            self._thread = threading.Thread(target=self._loop, daemon=True)
            self._thread.start()
            self.connected.emit(port)
            return True
        except Exception as e:
            self.line_received.emit(f'[오류] 연결 실패: {e}')
            return False

    def disconnect(self):
        self._running = False
        if self._ser:
            try: self._ser.close()
            except Exception: pass
            self._ser = None
        self.disconnected.emit()

    @property
    def is_open(self) -> bool:
        return self._ser is not None and self._ser.is_open

    # ── 쓰기 ─────────────────────────────────────────────
    def send_json(self, payload: dict) -> bool:
        if not self.is_open:
            return False
        data = json.dumps(payload, ensure_ascii=False).encode('utf-8')
        frame = bytes([STX]) + data + bytes([ETX])
        try:
            self._ser.write(frame)
            return True
        except Exception as e:
            self.line_received.emit(f'[오류] 전송 실패: {e}')
            return False

    def send_raw(self, text: str) -> bool:
        if not self.is_open:
            return False
        try:
            self._ser.write((text + '\n').encode('ascii', errors='replace'))
            return True
        except Exception as e:
            self.line_received.emit(f'[오류] 전송 실패: {e}')
            return False

    # ── 수신 루프 ─────────────────────────────────────────
    def _loop(self):
        while self._running and self._ser and self._ser.is_open:
            try:
                chunk = self._ser.read(self._ser.in_waiting or 1)
                if not chunk:
                    continue
                with self._lock:
                    self._buf.extend(chunk)
                    self._drain()
                    if len(self._buf) > BUF_MAX:
                        self._buf.clear()
            except Exception as e:
                if self._running:
                    self.line_received.emit(f'[오류] 수신: {e}')
                    self.disconnect()
                break

    def _drain(self):
        while self._buf:
            # STX...ETX JSON 프레임
            if self._buf[0] == STX:
                # manager_key/name 은 RFID 바이너리 → 0x03(ETX) 포함 가능.
                # ETX 후보를 하나씩 시험하면서 실제 JSON 끝인지 확인.
                parsed = False
                pos = 1
                while pos < len(self._buf):
                    if self._buf[pos] == ETX:
                        raw = bytes(self._buf[1:pos])
                        try:
                            # latin-1: 0x00~0xFF → 모두 디코딩 가능, UnicodeDecodeError 없음
                            text = raw.decode('latin-1')
                            obj  = json.loads(text)
                            del self._buf[:pos + 1]
                            self.json_received.emit(obj)
                            parsed = True
                            break
                        except json.JSONDecodeError:
                            # 이 ETX 는 JSON 본문 안에 있음 → 다음 ETX 탐색
                            pos += 1
                            continue
                        except Exception as e:
                            # 예상치 못한 오류 → 이 프레임 버리고 계속
                            self.line_received.emit(
                                f'[프레임 오류] {e}  raw={raw[:60]!r}')
                            del self._buf[:pos + 1]
                            parsed = True   # "consumed" 처리
                            break
                    pos += 1
                if not parsed:
                    break  # ETX 아직 도착 안 함 — 더 읽기 대기
            else:
                # 줄 단위 텍스트
                if b'\n' in self._buf:
                    nl = self._buf.index(b'\n')
                    line = bytes(self._buf[:nl]).decode('utf-8', errors='replace').strip()
                    del self._buf[:nl + 1]
                    if line:
                        self.line_received.emit(line)
                        # PSOk → 자동 인증
                        if 'PSOk' in line:
                            try: self._ser.write(b'Z\n')
                            except Exception: pass
                else:
                    break


# ═══════════════════════════════════════════════════════════
# 메인 윈도우
# ═══════════════════════════════════════════════════════════
class ArduinoConfigurator(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle('TraceQ Arduino Configurator  v2.0')
        self.setMinimumSize(760, 780)
        self.setStyleSheet(_DARK)

        self._worker = SerialWorker()
        self._worker.json_received.connect(self._on_json)
        self._worker.line_received.connect(self._on_line)
        self._worker.connected.connect(self._on_connected)
        self._worker.disconnected.connect(self._on_disconnected)

        self._build_ui()
        self._refresh_ports()

        # 현재 시간 1초마다 자동 갱신
        self._clock_timer = QTimer(self)
        self._clock_timer.timeout.connect(
            lambda: self.dt_edit.setDateTime(QDateTime.currentDateTime()))
        self._clock_timer.start(30000)

    # ── UI 구성 ───────────────────────────────────────────
    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setSpacing(8)
        root.setContentsMargins(10, 10, 10, 10)

        # ── 연결 바 ──────────────────────────────────────
        conn_grp = QGroupBox('연결')
        conn_lay = QHBoxLayout(conn_grp)
        conn_lay.setSpacing(6)

        self.port_combo = QComboBox(); self.port_combo.setMinimumWidth(110)
        self.btn_refresh = QPushButton('새로고침')
        self.btn_connect = QPushButton('연 결')
        self.btn_connect.setStyleSheet(_BTN_GREEN)
        self.btn_disconnect = QPushButton('해 제')
        self.btn_disconnect.setStyleSheet(_BTN_RED)
        self.btn_disconnect.setEnabled(False)
        self.conn_status = QLabel('● 연결 안 됨')
        self.conn_status.setStyleSheet('color:#ff6666; font-weight:bold;')

        conn_lay.addWidget(QLabel('포트:'))
        conn_lay.addWidget(self.port_combo)
        conn_lay.addWidget(self.btn_refresh)
        conn_lay.addWidget(self.btn_connect)
        conn_lay.addWidget(self.btn_disconnect)
        conn_lay.addStretch()
        conn_lay.addWidget(self.conn_status)
        root.addWidget(conn_grp)

        self.btn_refresh.clicked.connect(self._refresh_ports)
        self.btn_connect.clicked.connect(self._do_connect)
        self.btn_disconnect.clicked.connect(self._do_disconnect)

        # ── 설정 그룹 ────────────────────────────────────
        cfg_grp = QGroupBox('기기 설정')
        cfg_grid = QGridLayout(cfg_grp)
        cfg_grid.setSpacing(8)
        cfg_grid.setColumnStretch(1, 1)
        cfg_grid.setColumnStretch(3, 1)

        # 기기 타입
        self.type_combo = QComboBox()
        for code, label in DEVICE_TYPES:
            self.type_combo.addItem(label, code)
        cfg_grid.addWidget(QLabel('기기 타입:'), 0, 0)
        cfg_grid.addWidget(self.type_combo, 0, 1)

        # 기기 번호
        self.num_spin = QSpinBox(); self.num_spin.setRange(0, 99)
        cfg_grid.addWidget(QLabel('기기 번호:'), 0, 2)
        cfg_grid.addWidget(self.num_spin, 0, 3)

        # 세척 알람
        self.washing_spin = QSpinBox()
        self.washing_spin.setRange(1, 120); self.washing_spin.setSuffix(' 분')
        self.washing_spin.setValue(4)
        cfg_grid.addWidget(QLabel('세척 알람:'), 1, 0)
        cfg_grid.addWidget(self.washing_spin, 1, 1)

        # 소독 알람
        self.df_spin = QSpinBox()
        self.df_spin.setRange(1, 120); self.df_spin.setSuffix(' 분')
        self.df_spin.setValue(18)
        cfg_grid.addWidget(QLabel('소독 알람:'), 1, 2)
        cfg_grid.addWidget(self.df_spin, 1, 3)

        # 최대 소독 횟수
        self.df_max_spin = QSpinBox()
        self.df_max_spin.setRange(0, 9999)
        self.df_max_spin.setValue(100)
        cfg_grid.addWidget(QLabel('최대 소독횟수:'), 2, 0)
        cfg_grid.addWidget(self.df_max_spin, 2, 1)

        # 동시소독 딜레이
        self.df_delay_spin = QSpinBox()
        self.df_delay_spin.setRange(0, 999)
        cfg_grid.addWidget(QLabel('동시소독 딜레이:'), 2, 2)
        cfg_grid.addWidget(self.df_delay_spin, 2, 3)

        # 동시소독 슬롯
        self.df_slot_spin = QSpinBox()
        self.df_slot_spin.setRange(0, 99)
        self.df_slot_spin.setValue(1)
        cfg_grid.addWidget(QLabel('동시소독 슬롯:'), 3, 0)
        cfg_grid.addWidget(self.df_slot_spin, 3, 1)

        # 클리어 카운트
        self.df_clear_spin = QSpinBox()
        self.df_clear_spin.setRange(0, 9999)
        cfg_grid.addWidget(QLabel('클리어 카운트:'), 3, 2)
        cfg_grid.addWidget(self.df_clear_spin, 3, 3)

        # 알람 소리 / 환자 확인
        self.alarm_chk = QCheckBox('알람 소리')
        self.alarm_chk.setChecked(True)
        self.patient_chk = QCheckBox('환자 확인')
        cfg_grid.addWidget(self.alarm_chk, 4, 0, 1, 2)
        cfg_grid.addWidget(self.patient_chk, 4, 2, 1, 2)

        # 담당자 키 / 이름 (읽기 전용)
        self.mgr_key_edit = QLineEdit(); self.mgr_key_edit.setReadOnly(True)
        self.mgr_key_edit.setPlaceholderText('(읽기 전용)')
        self.mgr_name_edit = QLineEdit(); self.mgr_name_edit.setReadOnly(True)
        self.mgr_name_edit.setPlaceholderText('(읽기 전용)')
        cfg_grid.addWidget(QLabel('담당자 키:'), 5, 0)
        cfg_grid.addWidget(self.mgr_key_edit, 5, 1)
        cfg_grid.addWidget(QLabel('담당자 이름:'), 5, 2)
        cfg_grid.addWidget(self.mgr_name_edit, 5, 3)

        # 액교환일 (읽기 전용 — Arduino EEPROM ClearDateTime)
        self.clear_date_edit = QLineEdit()
        self.clear_date_edit.setReadOnly(True)
        self.clear_date_edit.setPlaceholderText('(설정 읽기 후 표시)')
        self.clear_date_edit.setStyleSheet('color:#88cc88;')
        cfg_grid.addWidget(QLabel('액교환일:'), 6, 0)
        cfg_grid.addWidget(self.clear_date_edit, 6, 1, 1, 3)

        root.addWidget(cfg_grp)

        # ── 날짜/시간 그룹 ────────────────────────────────
        dt_grp = QGroupBox('날짜 / 시간')
        dt_lay = QHBoxLayout(dt_grp)

        self.dt_edit = QDateTimeEdit(QDateTime.currentDateTime())
        self.dt_edit.setDisplayFormat('yyyy-MM-dd  HH:mm:ss')
        self.dt_edit.setCalendarPopup(True)
        self.dt_edit.setMinimumWidth(210)
        self.btn_pc_time = QPushButton('PC 시간으로')
        self.btn_pc_time.setStyleSheet(_BTN_BLUE)
        self.btn_send_dt = QPushButton('날짜/시간 전송')
        self.btn_send_dt.setStyleSheet(_BTN_ORANGE)
        self.btn_send_dt.setEnabled(False)

        dt_lay.addWidget(QLabel('현재 날짜 시간:'))
        dt_lay.addWidget(self.dt_edit)
        dt_lay.addWidget(self.btn_pc_time)
        dt_lay.addWidget(self.btn_send_dt)
        dt_lay.addStretch()
        root.addWidget(dt_grp)

        self.btn_pc_time.clicked.connect(
            lambda: self.dt_edit.setDateTime(QDateTime.currentDateTime()))
        self.btn_send_dt.clicked.connect(self._do_send_datetime)

        # ── 설정 읽기/저장 버튼 ───────────────────────────
        cfg_btn_lay = QHBoxLayout()
        self.btn_read   = QPushButton('⟳  설정 읽기')
        self.btn_write  = QPushButton('✔  설정 저장')
        self.btn_defaults = QPushButton('↺  초기값으로 설정')
        self.btn_read.setStyleSheet(_BTN_BLUE)
        self.btn_write.setStyleSheet(_BTN_GREEN)
        self.btn_defaults.setStyleSheet(_BTN_PURPLE)
        self.btn_read.setEnabled(False)
        self.btn_write.setEnabled(False)
        cfg_btn_lay.addWidget(self.btn_read)
        cfg_btn_lay.addWidget(self.btn_write)
        cfg_btn_lay.addWidget(self.btn_defaults)
        cfg_btn_lay.addStretch()
        root.addLayout(cfg_btn_lay)

        self.btn_read.clicked.connect(self._do_read_config)
        self.btn_write.clicked.connect(self._do_write_config)
        self.btn_defaults.clicked.connect(self._do_reset_defaults)

        # ── 태그 초기화 그룹 ──────────────────────────────
        tag_grp = QGroupBox('태그 초기화 (RFID 태그를 리더기에 올려놓고 버튼 클릭)')
        tag_lay = QHBoxLayout(tag_grp)

        self.btn_tag_factory = QPushButton('Factory → TraceQ  (신규 태그)')
        self.btn_tag_reset   = QPushButton('TraceQ 데이터 초기화')
        self.btn_tag_to_factory = QPushButton('TraceQ → Factory  (공장 초기화)')

        self.btn_tag_factory.setStyleSheet(_BTN_GREEN)
        self.btn_tag_reset.setStyleSheet(_BTN_ORANGE)
        self.btn_tag_to_factory.setStyleSheet(_BTN_RED)
        for b in (self.btn_tag_factory, self.btn_tag_reset, self.btn_tag_to_factory):
            b.setEnabled(False)

        tag_lay.addWidget(self.btn_tag_factory)
        tag_lay.addWidget(self.btn_tag_reset)
        tag_lay.addWidget(self.btn_tag_to_factory)
        root.addWidget(tag_grp)

        self.btn_tag_factory.clicked.connect(lambda: self._do_new_tag(0))
        self.btn_tag_reset.clicked.connect(lambda: self._do_new_tag(1))
        self.btn_tag_to_factory.clicked.connect(lambda: self._do_new_tag(2))

        # ── 로그 ─────────────────────────────────────────
        log_grp = QGroupBox('통신 로그')
        log_lay = QVBoxLayout(log_grp)
        self.log_edit = QTextEdit()
        self.log_edit.setReadOnly(True)
        self.log_edit.setMinimumHeight(160)
        btn_clear_log = QPushButton('로그 지우기')
        btn_clear_log.setFixedWidth(90)
        btn_clear_log.clicked.connect(self.log_edit.clear)
        log_lay.addWidget(self.log_edit)
        log_hlay = QHBoxLayout()
        log_hlay.addStretch()
        log_hlay.addWidget(btn_clear_log)
        log_lay.addLayout(log_hlay)
        root.addWidget(log_grp)

        self._set_controls_enabled(False)

    # ── 포트 목록 ─────────────────────────────────────────
    def _refresh_ports(self):
        try:
            import serial.tools.list_ports as lp
            ports = sorted(p.device for p in lp.comports())
        except Exception:
            ports = []
        self.port_combo.clear()
        for p in ports:
            self.port_combo.addItem(p)
        if not ports:
            self.port_combo.addItem('(포트 없음)')
            return
        # Arduino 포트가 있으면 자동 선택
        auto = find_arduino_port()
        if auto:
            idx = self.port_combo.findText(auto)
            if idx >= 0:
                self.port_combo.setCurrentIndex(idx)

    # ── 연결 / 해제 ───────────────────────────────────────
    def _do_connect(self):
        port = self.port_combo.currentText()
        if '(' in port:
            QMessageBox.warning(self, '연결', '유효한 포트를 선택하세요.')
            return
        self._log(f'연결 시도: {port} …')
        self._worker.connect(port)

    def _do_disconnect(self):
        self._worker.disconnect()

    def _on_connected(self, port: str):
        self.conn_status.setText(f'● 연결됨  ({port})')
        self.conn_status.setStyleSheet('color:#66ff66; font-weight:bold;')
        self.btn_connect.setEnabled(False)
        self.btn_disconnect.setEnabled(True)
        self._set_controls_enabled(True)
        self._log(f'✔ 연결 성공: {port}')
        # 연결 직후 설정 자동 읽기
        QTimer.singleShot(500, self._do_read_config)

    def _on_disconnected(self):
        self.conn_status.setText('● 연결 안 됨')
        self.conn_status.setStyleSheet('color:#ff6666; font-weight:bold;')
        self.btn_connect.setEnabled(True)
        self.btn_disconnect.setEnabled(False)
        self._set_controls_enabled(False)
        self._log('연결 해제됨')

    def _set_controls_enabled(self, en: bool):
        for w in (self.btn_read, self.btn_write, self.btn_send_dt,
                  self.btn_tag_factory, self.btn_tag_reset, self.btn_tag_to_factory):
            w.setEnabled(en)

    # ── 수신 처리 ─────────────────────────────────────────
    def _on_json(self, obj: dict):
        keys = list(obj.keys())
        self._log(f'◀ JSON 수신 ({len(keys)}개 키): {keys}')
        self._fill_form(obj)
        self._log('   → 폼 업데이트 완료')

    def _on_line(self, line: str):
        if line.strip():
            self._log(f'  {line}')

    # ── 설정 폼 채우기 ────────────────────────────────────
    @staticmethod
    def _safe_str(v) -> str:
        """ArduinoJson 응답에서 문자열 추출.
        - bytes/bytearray : UTF-8 디코딩 + null 제거
        - list[int]       : unsigned char[] 가 int 배열로 직렬화된 경우
        - 일반 str        : null 제거 후 반환 (base64 자동 감지 없음)
        """
        if isinstance(v, (bytes, bytearray)):
            return v.decode('utf-8', errors='replace').rstrip('\x00').strip()
        if isinstance(v, list):
            try:
                return bytes(int(x) & 0xFF for x in v).decode(
                    'utf-8', errors='replace').rstrip('\x00').strip()
            except Exception:
                return ''
        if isinstance(v, str):
            return v.rstrip('\x00').strip()
        return str(v) if v is not None else ''

    @staticmethod
    def _safe_int(v, default: int) -> int:
        """None/빈값은 default, 그 외 int 변환 (0도 그대로 반환)."""
        if v is None:
            return default
        try:
            return int(v)
        except (TypeError, ValueError):
            return default

    def _fill_form(self, d: dict):
        # 기기 타입 — Arduino 펌웨어는 char[1] 오버플로우로 여분 문자 포함 가능
        raw_type = d.get('device_type', '')
        dtype = str(raw_type).strip() if raw_type is not None else ''
        if dtype:
            # 첫 글자만 사용 (S/G/W/D 중 하나)
            first = dtype[0].upper()
            if first in ('S', 'G', 'W', 'D'):
                idx = self.type_combo.findData(first)
                if idx >= 0:
                    self.type_combo.setCurrentIndex(idx)

        # 숫자형 설정 (0 도 정상값으로 처리)
        self.num_spin.setValue(    self._safe_int(d.get('device_number'), 0))
        self.washing_spin.setValue(max(1, self._safe_int(d.get('washing_time'), 4)))
        self.df_spin.setValue(     max(1, self._safe_int(d.get('df_time'),      18)))
        self.df_max_spin.setValue( self._safe_int(d.get('df_max_cnt'),   100))
        self.df_delay_spin.setValue(self._safe_int(d.get('df_sim_delay'), 0))
        self.df_slot_spin.setValue( self._safe_int(d.get('df_sim_slot'),  1))
        self.df_clear_spin.setValue(self._safe_int(d.get('df_clear_cnt'), 0))

        # 불리언
        self.alarm_chk.setChecked(bool(d.get('alarm_sound', True)))
        self.patient_chk.setChecked(bool(d.get('patient_check', False)))

        # 담당자 (읽기 전용 — bytes / base64 / str 모두 처리)
        self.mgr_key_edit.setText( self._safe_str(d.get('manager_key',  '')))
        self.mgr_name_edit.setText(self._safe_str(d.get('manager_name', '')))

        # 액교환일 (df_clear_date_time — EEPROM ClearDateTime)
        clear_dt = str(d.get('df_clear_date_time', '') or '').strip()
        if clear_dt and clear_dt != 'YYYY-MM-DD hh:mm:ss':
            self.clear_date_edit.setText(clear_dt)
        else:
            self.clear_date_edit.setText('')


    # ── 설정 읽기 ─────────────────────────────────────────
    def _do_read_config(self):
        ok = self._worker.send_json({'cmd': 'cfg_get_config'})
        if ok:
            self._log('▶ cfg_get_config 전송')
        else:
            self._log('[경고] 전송 실패 — 연결 상태 확인')

    # ── 설정 저장 ─────────────────────────────────────────
    def _do_write_config(self):
        dtype_code = self.type_combo.currentData() or 'S'
        now_str = self.dt_edit.dateTime().toString('yyyy-MM-dd HH:mm:ss')
        payload = {
            'cmd':               'cfg_set_config',
            'device_type':       dtype_code,
            'device_number':     self.num_spin.value(),
            'alarm_sound':       self.alarm_chk.isChecked(),
            'washing_time':      self.washing_spin.value(),
            'df_time':           self.df_spin.value(),
            'df_max_cnt':        self.df_max_spin.value(),
            'df_sim_delay':      self.df_delay_spin.value(),
            'df_sim_slot':       self.df_slot_spin.value(),
            'df_clear_cnt':      self.df_clear_spin.value(),
            'patient_check':     self.patient_chk.isChecked(),
            'device_date_time':  now_str,
        }
        ok = self._worker.send_json(payload)
        if ok:
            self._log(f'▶ cfg_set_config 전송  (타입={dtype_code}, 세척={self.washing_spin.value()}분, 소독={self.df_spin.value()}분)')
        else:
            self._log('[경고] 전송 실패')

    # ── 펌웨어 초기값으로 폼 리셋 ────────────────────────────
    def _do_reset_defaults(self):
        """펌웨어 최초 업로드 시와 동일한 초기값으로 폼을 되돌린다.

        출처 (각 NVM 클래스 기본값):
          DeviceOption      : type='W'(세척기), number=1
          AlarmOption       : sound=True, washing_time=4분, df_time=18분
          DisinfectionOption: max_cnt=30, sim_delay=3분, sim_slot=1, clear_cnt=0
          RecordOption      : patient_check=True
        """
        # 기기 타입 → 세척기 ('W')
        idx = self.type_combo.findData('W')
        if idx >= 0:
            self.type_combo.setCurrentIndex(idx)

        # 기기 번호
        self.num_spin.setValue(1)

        # 알람 타이머
        self.washing_spin.setValue(4)
        self.df_spin.setValue(18)

        # 소독 옵션
        self.df_max_spin.setValue(30)
        self.df_delay_spin.setValue(3)
        self.df_slot_spin.setValue(1)
        self.df_clear_spin.setValue(0)

        # 플래그
        self.alarm_chk.setChecked(True)
        self.patient_chk.setChecked(True)

        # 담당자 필드 초기화
        self.mgr_key_edit.clear()
        self.mgr_name_edit.clear()

        self._log('↺ 폼을 펌웨어 초기값으로 리셋했습니다.'
                  '  (저장하려면 [설정 저장] 클릭)')

    # ── 날짜/시간 전송 ────────────────────────────────────
    def _do_send_datetime(self):
        dt_str = self.dt_edit.dateTime().toString('yyyy-MM-dd HH:mm:ss')
        ok = self._worker.send_json({'cmd': 'cfg_set_date_time', 'device_date_time': dt_str})
        if ok:
            self._log(f'▶ cfg_set_date_time 전송: {dt_str}')

    # ── 태그 초기화 ───────────────────────────────────────
    def _do_new_tag(self, type_id: int):
        labels = {
            0: 'Factory→TraceQ (신규 태그)',
            1: 'TraceQ 데이터 초기화',
            2: 'TraceQ→Factory (공장 초기화)',
        }
        if type_id == 2:
            ans = QMessageBox.question(
                self, '확인',
                'TraceQ → Factory 초기화를 진행하시겠습니까?\n태그의 모든 TraceQ 데이터가 삭제됩니다.',
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
            )
            if ans != QMessageBox.StandardButton.Yes:
                return
        ok = self._worker.send_json({'cmd': 'cfg_new_tag', 'type_id': type_id})
        if ok:
            self._log(f'▶ cfg_new_tag (type_id={type_id})  [{labels[type_id]}]')
            self._log('   태그를 리더기에 올려두세요 — 4초 이내 처리됩니다.')

    # ── 로그 ─────────────────────────────────────────────
    def _log(self, msg: str):
        ts = datetime.now().strftime('%H:%M:%S')
        line = f'[{ts}]  {msg}'
        self.log_edit.append(line)
        sb = self.log_edit.verticalScrollBar()
        sb.setValue(sb.maximum())

    # ── 표시 시 자동 연결 ─────────────────────────────────
    def showEvent(self, event):
        super().showEvent(event)
        # 창이 뜨고 300ms 후 자동 연결 시도 (1회만)
        QTimer.singleShot(300, self._auto_connect_once)

    def _auto_connect_once(self):
        if self._worker.is_open:
            return
        port = find_arduino_port()
        if port:
            self._log(f'[자동감지] Arduino 발견: {port}  →  자동 연결 중…')
            idx = self.port_combo.findText(port)
            if idx >= 0:
                self.port_combo.setCurrentIndex(idx)
            self._worker.connect(port)
        else:
            self._log('[자동감지] Arduino 포트 없음 — 포트를 선택 후 수동 연결하세요.')

    # ── 닫기 ─────────────────────────────────────────────
    def closeEvent(self, event):
        self._worker.disconnect()
        super().closeEvent(event)


# ─── 엔트리 포인트 ────────────────────────────────────────
def main():
    app = QApplication(sys.argv)
    app.setFont(QFont('맑은 고딕', 9))
    win = ArduinoConfigurator()
    win.show()
    sys.exit(app.exec())


if __name__ == '__main__':
    main()
