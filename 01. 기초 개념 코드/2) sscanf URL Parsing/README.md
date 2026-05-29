# ConsoleApplication2 — sscanf로 URL 문자열 파싱

## 무엇을 하는 코드인가
한 줄로 입력받은 URL 형식 문자열(`프로토콜://도메인:포트`)을 **`sscanf`의 형식 지정자**로 분해하는 예제입니다.

소스: [ConsoleApplication2.cpp](ConsoleApplication2/ConsoleApplication2.cpp)

```c
char basestr[1000];
int  port;
char protocol[100];
char domain[200];

scanf("%[^\n]", basestr);   // 개행 전까지 한 줄 전체 입력

int res = sscanf(basestr, "%[^:]://%[^:]:%d", protocol, domain, &port);
```

입력 예: `http://www.example.com:8080`
출력:
```
Protocol: http
Domain: www.example.com
Port: 8080
sscanf result: 3
```

## 핵심 개념
- **`%[^:]` (스캔셋, scanset)**: `:`가 나오기 전까지의 문자를 모두 읽음. `^`는 "제외(부정)"을 의미.
- **`%[^\n]`**: 개행 전까지 → 공백을 포함한 한 줄 전체를 받을 때 사용 (`%s`는 공백에서 끊김).
- **리터럴 매칭**: 형식 문자열의 `://`, `:`는 입력에서 그대로 일치해야 하는 구분자.
- **`sscanf` 반환값**: 성공적으로 변환·대입한 항목 수. 위 예제는 3개(`protocol`, `domain`, `port`)가 정상이면 `3`.
- `_CRT_SECURE_NO_WARNINGS`: MSVC에서 `scanf`/`sscanf`의 보안 경고(C4996)를 끄기 위한 매크로.

## 학습 포인트 / 주의
- 버퍼 오버플로 방지를 위해 실무에서는 `%99[^:]`처럼 **최대 길이를 지정**하는 습관이 좋습니다.
- 입력 형식이 어긋나면 반환값으로 어디까지 파싱됐는지 확인할 수 있습니다.
