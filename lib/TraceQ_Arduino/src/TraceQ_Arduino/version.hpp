#pragma once

/**
 * \brief TraceQ Arduino 펌웨어 버전.
 *
 * 2.0.0 — 1.0(=1.4.1) 코드베이스를 분석해 RFID 안전성/속도를 재설계 (미완).
 * 2.1.0 — 1.4.1 전 기능 완성판: legacy 서버 프로토콜 전체 이식, raw 시리얼
 *         수신 복원(현장 PC 호환), 소독기 RTC 자동 복구 원복, 태그 발급
 *         키 전환(공장↔TraceQ) 완성, 1.4.1 승계 버그(스택 오버런 6곳,
 *         소독기 매니저 인스턴스 불일치 등) 수정. nested auth 수정 실기 합격.
 * 2.2.0 — 서버 Latest 갈래 삭제(사용자 확정): 서버는 레거시 블록 덤프
 *         단일 경로. Version(Latest/Old) 메뉴·EEPROM 플래그(주소 0)·latest
 *         스텁 제거, 부팅 PSOk 는 서버 타입이면 항상 송신.
 * 2.2.1 — LCD 날짜/시간 저장 시 범위 검증 (월 1~12·일 1~31·시 0~23·분초
 *         0~59) — 1.0부터 있던 월 99 등 쓰레기 값 RTC 저장 구멍 보강.
 *         잘못된 값은 저장하지 않고 Invalid Date/Time 경고 후 폐기.
 * 2.2.2 — RTC 배터리 방전 시 2026-01-01 00:00:00 고정 (1.0은 컴파일 시각).
 *         고정 날짜 = 배터리 방전 식별 마커 + 소독기 RTC 자동 복구 확실 발동.
 * 2.2.3 — 새 빌드 업로드 시 EEPROM 완전 초기화(설정값 포함, 사용자 확정).
 *         빌드 도장(컴파일 시각 해시, 주소 4088) 불일치 = 새 펌웨어 첫 부팅
 *         → 전체 소거+기본값. 1.0의 DATA_NEEDS_INIT(4095) 방식은 구버전이
 *         깔려 있던 기기에서 초기화를 건너뛰어 이중 업로드가 필요했다.
 */
#define TRACEQ_VERSION_MAJOR 2
#define TRACEQ_VERSION_MINOR 2
#define TRACEQ_VERSION_PATCH 3
#define TRACEQ_VERSION_STRING "2.2.3"

// 1.0 UI가 사용하던 매크로 이름 — 호환을 위해 별칭 유지.
#define TRACEQ_ARDUINO_VERSION TRACEQ_VERSION_STRING
