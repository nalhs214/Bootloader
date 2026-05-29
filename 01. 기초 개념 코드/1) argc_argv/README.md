# ConsoleApplication1 — 명령행 인자(argc / argv) 출력

## 무엇을 하는 코드인가
프로그램 실행 시 전달되는 **명령행 인자(command-line arguments)** 를 순서대로 모두 출력하는 가장 기초적인 예제입니다.

소스: [ConsoleApplication1.cpp](ConsoleApplication1/ConsoleApplication1.cpp)

```c
int main(int argc, char* argv[])
{
    for (int i = 0; i < argc; i++)
    {
        printf("argv[%d] = %s\n", i, argv[i]);
    }
    return 0;
}
```

## 핵심 개념
- **`argc` (argument count)**: 전달된 인자의 개수. 최소 1 (프로그램 경로 자체가 `argv[0]`).
- **`argv` (argument vector)**: 인자 문자열들의 배열. `argv[0]`은 실행 파일 경로, `argv[1]`부터 사용자가 입력한 인자.
- `main` 함수가 인자를 받을 수 있다는 점과, `for` 루프로 문자열 배열을 순회하는 기본 패턴.

## 실행 예시
```
> ConsoleApplication1.exe hello 123
argv[0] = ...\ConsoleApplication1.exe
argv[1] = hello
argv[2] = 123
```
Visual Studio에서 인자를 주려면: **프로젝트 속성 → 디버깅 → 명령 인수**에 값을 입력합니다.
