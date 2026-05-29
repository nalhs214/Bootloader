# sscanf_data_parsing — CSV 파싱 + 동적 메모리 할당 (2-pass)

## 무엇을 하는 코드인가
[ConsoleApplication3](../ConsoleApplication3)의 발전형입니다. 같은 Pick & Place CSV를 파싱하되,
**고정 크기 배열 대신 `malloc`으로 필요한 만큼만 메모리를 동적 할당**하는 "2-패스(두 번 읽기)" 기법을 보여줍니다.

소스: [sscanf_data_parsing.cpp](sscanf_data_parsing/sscanf_data_parsing.cpp)
데이터: [Pick Place for NU40-DK-Basic(Default).csv](sscanf_data_parsing/Pick%20Place%20for%20NU40-DK-Basic(Default).csv)

## 핵심 아이디어: 2-pass 처리
데이터 개수를 미리 알 수 없을 때, 파일을 두 번 읽어 메모리를 낭비 없이 사용합니다.

```c
Data* nu40data = NULL;   // 동적 배열 (개수를 센 뒤 할당)
Data  _nu40data;         // 1차 패스용 임시 버퍼
```

1. **1차 패스 — 개수 세기**
   파일을 끝까지 읽으며 유효한 줄(`sscanf` 반환값 == 7)의 개수 `cnt`만 카운트.
   읽은 값은 임시 변수 `_nu40data`에 넣어 버림.

2. **메모리 할당**
   ```c
   nu40data = (Data*)malloc(sizeof(Data) * cnt);  // 딱 필요한 만큼만
   ```

3. **파일 포인터 되감기**
   ```c
   fseek(fp, 0, SEEK_SET);   // 파일 처음으로 이동
   ```

4. **2차 패스 — 실제 저장**
   다시 한 줄씩 읽어 할당한 배열 `nu40data[readcnt]`에 채움.

5. 출력 후 `free(nu40data)`로 메모리 해제.

## 핵심 개념
- **동적 메모리 할당 `malloc` / `free`**: 컴파일 타임에 크기를 모를 때 런타임에 메모리 확보·해제.
- **`fseek(fp, 0, SEEK_SET)`**: 같은 파일 스트림을 다시 처음부터 읽기 위한 되감기.
- **2-pass 패턴**: 1차로 크기 측정 → 할당 → 2차로 채우기. 메모리 효율과 안전성을 동시에 확보.
- **에러 처리 분리**: `file_not_exists`, `memerr` 라벨로 `goto` 기반 에러 흐름 정리.

## 주의 / 개선 포인트
- 현재 `fopen("__", "r")`의 파일명이 플레이스홀더(`"__"`)로 되어 있습니다.
  실제 실행 시 CSV 파일명(`"Pick Place for NU40-DK-Basic(Default).csv"`)으로 바꿔야 합니다.
- `malloc` 실패 시 `goto memerr`로 파일을 닫고 종료 — 자원 누수 방지의 좋은 습관.

## ConsoleApplication3와의 차이
| 항목 | ConsoleApplication3 | sscanf_data_parsing |
|------|---------------------|---------------------|
| 저장 방식 | 고정 배열 `Data[150]` | 동적 배열 `malloc` |
| 파일 읽기 | 1회 | 2회 (2-pass) |
| 메모리 효율 | 고정(낭비/초과 위험) | 데이터 수에 딱 맞춤 |
