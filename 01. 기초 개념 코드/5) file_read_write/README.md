# file_read_write — 파일 쓰기/읽기 기초 (작성 중)

## 무엇을 하는 코드인가
텍스트 파일에 **쓰기(write)** 한 뒤 다시 **읽기(read)** 하는 파일 입출력의 기본기를 연습하는 예제입니다.
현재 코드는 **작성 중(미완성)** 상태입니다.

소스: [file_read_write.cpp](file_read_write/file_read_write.cpp)

```c
FILE* fp = fopen("sample.txt", "w");   // 쓰기 모드로 열기

fputs("test\n", fp);                   // 문자열 쓰기
fprintf(fp, "printf test : %d\n", 00); // 서식 지정 쓰기
fclose(fp);

FILE* fp = fopen("sample.txt", "r");   // 읽기 모드로 다시 열기
for (int i = 0; i < )                  // ← 미완성
fclose(fp);
```

## 핵심 개념
- **`fopen` 모드**
  - `"w"` : 쓰기 (파일이 있으면 내용 삭제 후 새로 작성)
  - `"r"` : 읽기
- **파일 쓰기 함수**
  - `fputs` : 문자열을 그대로 기록
  - `fprintf` : `printf`처럼 서식을 지정해 기록
- **`fclose`** : 사용한 파일 스트림을 닫아 버퍼를 비우고 자원 반환.

## 미완성 / 수정 필요 사항
이 파일은 학습 도중 단계라 그대로는 **컴파일되지 않습니다.** 다음을 보완해야 합니다.
1. **변수 중복 선언**: `FILE* fp`가 두 번 선언됨 → 두 번째는 `fp = fopen(...)`로 재대입하거나 다른 이름 사용.
2. **불완전한 for 문**: `for (int i = 0; i < )` → 조건/증감식과 본문이 비어 있음.
3. **읽기 로직 부재**: `fgets`/`fscanf` 등으로 `sample.txt`를 다시 읽어 출력하는 부분 추가 필요.
4. **파일 열기 실패 처리**: 주석 처리된 `if (!fp)` 검사를 활성화하는 것이 안전.

### 완성 예시 (참고)
```c
FILE* fp = fopen("sample.txt", "w");
if (!fp) { printf("파일 열기 실패\n"); return 1; }
fputs("test\n", fp);
fprintf(fp, "printf test : %d\n", 0);
fclose(fp);

fp = fopen("sample.txt", "r");           // 같은 포인터 재사용
if (!fp) { printf("파일 열기 실패\n"); return 1; }
char line[60];
while (fgets(line, sizeof(line), fp)) {  // 한 줄씩 읽어 출력
    printf("%s", line);
}
fclose(fp);
```
