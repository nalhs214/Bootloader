# ConsoleApplication3 — CSV 파일 읽어 구조체 배열로 파싱

## 무엇을 하는 코드인가
Altium Designer가 생성한 **Pick & Place CSV 파일**을 한 줄씩 읽어, 각 부품 정보를 구조체 배열에 저장하고 출력하는 예제입니다.

소스: [ConsoleApplication3.cpp](ConsoleApplication3/ConsoleApplication3.cpp)
데이터: [Pick Place for NU40-DK-Basic(Default).csv](ConsoleApplication3/Pick%20Place%20for%20NU40-DK-Basic(Default).csv)

## 데이터 구조
```c
typedef struct Place_Locations
{
    char   Designator[10];   // 부품 지정자 (예: C1, R2)
    char   Comment[50];      // 부품 값/코멘트
    char   Layer[20];        // 실장 면 (TopLayer 등)
    double Center_X;         // X 좌표
    double Center_Y;         // Y 좌표
    int    Rotation;         // 회전 각도
    char   Description[100]; // 설명
} Data;

Data nu40data[150];          // 고정 크기 배열 (최대 150개)
```

## 동작 흐름
1. `fopen`으로 CSV 파일을 읽기 모드(`"r"`)로 연다. 실패 시 `goto`로 에러 메시지 출력.
2. `while (!feof(fp))` 루프에서 `fgets`로 한 줄씩 읽는다.
3. `sscanf`의 스캔셋으로 `"..."` 따옴표로 감싼 7개 필드를 분해한다.
   ```c
   sscanf(pstr, "\"%[^\"]\",\"%[^\"]\",\"%[^\"]\",\"%lf\",\"%lf\",\"%d\",\"%[^\"]", ...);
   ```
4. 반환값 `n == 7`(7개 모두 변환 성공)일 때만 유효 데이터로 인정하고 `cnt++`.
5. 모은 데이터를 `/` 구분자로 출력.

## 핵심 개념
- **`fopen` / `fgets` / `fclose`**: 텍스트 파일을 한 줄 단위로 읽는 기본 패턴.
- **`feof`**: 파일 끝(EOF) 도달 여부 확인.
- **`%[^\"]` 스캔셋**: 따옴표(`"`) 전까지의 문자열을 읽어 따옴표로 감싼 CSV 필드를 추출.
- **`sscanf` 반환값으로 유효성 검사**: 헤더·빈 줄 등 형식이 안 맞는 줄을 자연스럽게 걸러냄.
- **`%lf`(double), `%d`(int)** 등 자료형별 형식 지정자.

## 코드 안에 남긴 질문 (학습 메모)
- 버퍼 크기는 어떻게 잡는 것이 좋은가?
- 형식이 깨진 줄을 `sscanf` 반환값 `== 7` 비교로 거르는 방식이 적절한가?
  → 적절한 접근입니다. 다만 고정 배열(150개)은 데이터가 많아지면 넘칠 수 있어,
    동적 할당 버전인 [sscanf_data_parsing](../sscanf_data_parsing)에서 이를 개선합니다.
