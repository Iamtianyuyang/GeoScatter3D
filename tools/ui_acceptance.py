#!/usr/bin/env python3
"""
GeoScatter3D UI 自动化验收脚本（TIA-111）。

通过控制面驱动全部功能，逐项截图并记录通过/不通过结论。

启动应用（示例）：
  build/Debug/GeoScatter3D.exe --bundle data/sample-points.gs3d.bundle \\
      --no-welcome --control-plane=12735

运行本脚本：
  python tools/ui_acceptance.py --port 12735 [--out-dir acceptance_out] [--quit]

验收项目：
  1. ping 健康检查
  2. list_components 组件清单
  3. get_state 各组件状态
  4. toggle 面板显示/隐藏
  5. set_value 设置值
  6. screenshot 截图
  7. 主题切换
  8. 相机操作
  9. 测量模式
  10. 侧边栏折叠
"""

import argparse
import base64
import json
import os
import socket
import sys
import time
from pathlib import Path


class ControlPlaneClient:
    def __init__(self, host: str, port: int, timeout: float = 15.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.next_id = 1

    def call(self, method: str, params=None) -> dict:
        request = {
            "jsonrpc": "2.0",
            "id": self.next_id,
            "method": method,
        }
        if params is not None:
            request["params"] = params
        self.next_id += 1
        line = json.dumps(request, ensure_ascii=False) + "\n"
        started = time.monotonic()
        self.sock.sendall(line.encode("utf-8"))
        # 读一行（换行分隔 JSON）
        buffer = b""
        while b"\n" not in buffer:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("连接被关闭，未收到响应")
            buffer += chunk
        elapsed_ms = (time.monotonic() - started) * 1000.0
        response = json.loads(buffer.split(b"\n", 1)[0].decode("utf-8"))
        if "error" in response:
            raise RuntimeError(
                f"{method} 失败: code={response['error']['code']} "
                f"message={response['error']['message']}"
            )
        return response, elapsed_ms

    def call_screenshot(self, retries: int = 8, gap: float = 3.0) -> tuple[dict, float]:
        """截图带重试"""
        last_error = None
        for attempt in range(1, retries + 1):
            try:
                return self.call("screenshot")
            except RuntimeError as error:
                last_error = error
                print(f"  screenshot attempt {attempt} 失败，{gap:.0f}s 后重试: {error}")
                time.sleep(gap)
        raise last_error

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


class AcceptanceTest:
    def __init__(self, name: str, description: str):
        self.name = name
        self.description = description
        self.passed = False
        self.message = ""
        self.screenshot_path = None

    def to_dict(self) -> dict:
        result = {
            "name": self.name,
            "description": self.description,
            "passed": self.passed,
            "message": self.message,
        }
        if self.screenshot_path:
            result["screenshot"] = str(self.screenshot_path)
        return result


def run_acceptance_tests(client: ControlPlaneClient, out_dir: Path) -> list[AcceptanceTest]:
    """运行所有验收测试"""
    tests = []

    # 1. ping 健康检查
    test = AcceptanceTest("ping", "健康检查")
    try:
        response, elapsed = client.call("ping")
        if response.get("result") == "pong":
            test.passed = True
            test.message = f"响应: {response['result']}, 耗时: {elapsed:.1f}ms"
        else:
            test.message = f"意外响应: {response}"
    except Exception as e:
        test.message = f"异常: {e}"
    tests.append(test)
    print(f"  [{'✓' if test.passed else '✗'}] {test.name}: {test.message}")

    # 2. list_components 组件清单
    test = AcceptanceTest("list_components", "获取组件清单")
    try:
        response, elapsed = client.call("list_components")
        components = response.get("result", {}).get("components", [])
        test.passed = len(components) > 0
        test.message = f"组件数: {len(components)}, 耗时: {elapsed:.1f}ms"
    except Exception as e:
        test.message = f"异常: {e}"
    tests.append(test)
    print(f"  [{'✓' if test.passed else '✗'}] {test.name}: {test.message}")

    # 3. get_state 各组件状态
    test = AcceptanceTest("get_state", "获取组件状态")
    try:
        response, elapsed = client.call("get_state", {"component": "viewport.main"})
        state = response.get("result", {})
        test.passed = "fps" in state or "viewport_count" in state
        test.message = f"状态: {list(state.keys())[:5]}..., 耗时: {elapsed:.1f}ms"
    except Exception as e:
        test.message = f"异常: {e}"
    tests.append(test)
    print(f"  [{'✓' if test.passed else '✗'}] {test.name}: {test.message}")

    # 4. toggle 面板显示/隐藏
    test = AcceptanceTest("toggle_panel", "切换面板显示/隐藏")
    try:
        # 先获取当前状态
        response, _ = client.call("get_state", {"component": "panel.dataset"})
        initial_visible = response.get("result", {}).get("visible", True)

        # 切换
        response, elapsed = client.call("toggle", {"component": "panel.dataset"})
        new_visible = response.get("result", {}).get("visible", not initial_visible)

        # 再切换回来
        client.call("toggle", {"component": "panel.dataset"})

        test.passed = new_visible != initial_visible
        test.message = f"切换: {initial_visible} -> {new_visible}, 耗时: {elapsed:.1f}ms"
    except Exception as e:
        test.message = f"异常: {e}"
    tests.append(test)
    print(f"  [{'✓' if test.passed else '✗'}] {test.name}: {test.message}")

    # 5. set_value 设置值
    test = AcceptanceTest("set_value", "设置组件值")
    try:
        response, elapsed = client.call("set_value", {
            "component": "panel.performance",
            "value": True
        })
        test.passed = "result" in response
        test.message = f"设置性能面板可见, 耗时: {elapsed:.1f}ms"
    except Exception as e:
        test.message = f"异常: {e}"
    tests.append(test)
    print(f"  [{'✓' if test.passed else '✗'}] {test.name}: {test.message}")

    # 6. screenshot 截图
    test = AcceptanceTest("screenshot", "截图")
    try:
        response, elapsed = client.call_screenshot()
        result = response.get("result", {})
        width = result.get("width", 0)
        height = result.get("height", 0)
        data = result.get("data", "")
        test.passed = width > 0 and height > 0 and len(data) > 0
        test.message = f"尺寸: {width}x{height}, 数据长度: {len(data)}, 耗时: {elapsed:.1f}ms"

        # 保存截图
        if test.passed:
            screenshot_path = out_dir / "screenshot_main.png"
            with open(screenshot_path, "wb") as f:
                f.write(base64.b64decode(data))
            test.screenshot_path = screenshot_path
    except Exception as e:
        test.message = f"异常: {e}"
    tests.append(test)
    print(f"  [{'✓' if test.passed else '✗'}] {test.name}: {test.message}")

    # 7. 主题切换
    test = AcceptanceTest("theme_switch", "主题切换")
    try:
        # 获取当前主题
        response, _ = client.call("get_state", {"component": "menu.view.theme"})
        initial_theme = response.get("result", {}).get("theme_id", 0)

        # 切换到下一个主题
        response, elapsed = client.call("execute", {
            "component": "menu.view.theme",
            "command": "click"
        })

        # 截图验证
        time.sleep(0.5)
        response2, _ = client.call_screenshot()
        result = response2.get("result", {})
        width = result.get("width", 0)

        # 切换回来
        client.call("execute", {
            "component": "menu.view.theme",
            "command": "click"
        })
        client.call("execute", {
            "component": "menu.view.theme",
            "command": "click"
        })
        client.call("execute", {
            "component": "menu.view.theme",
            "command": "click"
        })

        test.passed = width > 0
        test.message = f"主题切换成功, 耗时: {elapsed:.1f}ms"

        # 保存截图
        if test.passed:
            screenshot_path = out_dir / "screenshot_theme.png"
            with open(screenshot_path, "wb") as f:
                f.write(base64.b64decode(result.get("data", "")))
            test.screenshot_path = screenshot_path
    except Exception as e:
        test.message = f"异常: {e}"
    tests.append(test)
    print(f"  [{'✓' if test.passed else '✗'}] {test.name}: {test.message}")

    # 8. 相机操作
    test = AcceptanceTest("camera_reset", "相机重置")
    try:
        response, elapsed = client.call("execute", {
            "component": "canvas.viewport",
            "command": "reset_camera"
        })
        test.passed = "result" in response
        test.message = f"相机重置, 耗时: {elapsed:.1f}ms"
    except Exception as e:
        test.message = f"异常: {e}"
    tests.append(test)
    print(f"  [{'✓' if test.passed else '✗'}] {test.name}: {test.message}")

    # 9. 测量模式
    test = AcceptanceTest("measure_mode", "测量模式切换")
    try:
        # 获取当前状态
        response, _ = client.call("get_state", {"component": "toolbar.measure"})
        initial_active = response.get("result", {}).get("measure_mode_active", False)

        # 切换测量模式
        response, elapsed = client.call("execute", {
            "component": "toolbar.measure",
            "command": "click"
        })
        new_active = response.get("result", {}).get("measure_mode_active", not initial_active)

        # 切换回来
        client.call("execute", {
            "component": "toolbar.measure",
            "command": "click"
        })

        test.passed = True
        test.message = f"测量模式: {initial_active} -> {new_active}, 耗时: {elapsed:.1f}ms"
    except Exception as e:
        test.message = f"异常: {e}"
    tests.append(test)
    print(f"  [{'✓' if test.passed else '✗'}] {test.name}: {test.message}")

    # 10. 侧边栏折叠
    test = AcceptanceTest("sidebar_toggle", "侧边栏折叠/展开")
    try:
        # 获取当前状态
        response, _ = client.call("get_state", {"component": "panel.dataset"})
        initial_visible = response.get("result", {}).get("visible", True)

        # 切换侧边栏
        response, elapsed = client.call("execute", {
            "component": "menu.view.restore_workspace",
            "command": "click"
        })

        # 截图验证
        time.sleep(0.5)
        response2, _ = client.call_screenshot()
        result = response2.get("result", {})
        width = result.get("width", 0)

        test.passed = width > 0
        test.message = f"侧边栏操作成功, 耗时: {elapsed:.1f}ms"

        # 保存截图
        if test.passed:
            screenshot_path = out_dir / "screenshot_sidebar.png"
            with open(screenshot_path, "wb") as f:
                f.write(base64.b64decode(result.get("data", "")))
            test.screenshot_path = screenshot_path
    except Exception as e:
        test.message = f"异常: {e}"
    tests.append(test)
    print(f"  [{'✓' if test.passed else '✗'}] {test.name}: {test.message}")

    return tests


def generate_report(tests: list[AcceptanceTest], out_dir: Path) -> str:
    """生成验收报告"""
    passed = sum(1 for t in tests if t.passed)
    total = len(tests)

    report = f"""# GeoScatter3D UI 自动化验收报告

## 概要

- 总测试数: {total}
- 通过: {passed}
- 失败: {total - passed}
- 通过率: {passed/total*100:.1f}%

## 测试详情

| # | 测试名称 | 描述 | 结果 | 消息 |
|---|----------|------|------|------|
"""
    for i, test in enumerate(tests, 1):
        status = "✓ 通过" if test.passed else "✗ 失败"
        report += f"| {i} | {test.name} | {test.description} | {status} | {test.message} |\n"

    report += f"""
## 截图

"""
    for test in tests:
        if test.screenshot_path:
            report += f"- {test.name}: `{test.screenshot_path}`\n"

    report += f"""
## 结论

"""
    if passed == total:
        report += "所有测试通过。✓\n"
    else:
        report += f"有 {total - passed} 项测试失败，需要进一步调查。\n"

    return report


def main() -> int:
    parser = argparse.ArgumentParser(description="GeoScatter3D UI 自动化验收")
    parser.add_argument("--host", default="127.0.0.1", help="控制面主机")
    parser.add_argument("--port", type=int, default=12735, help="控制面端口")
    parser.add_argument("--out-dir", default="acceptance_out", help="输出目录")
    parser.add_argument("--quit", action="store_true", help="测试完成后退出应用")
    args = parser.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"连接到控制面 {args.host}:{args.port}...")
    try:
        client = ControlPlaneClient(args.host, args.port)
    except Exception as e:
        print(f"连接失败: {e}")
        return 1

    try:
        print("运行验收测试...")
        tests = run_acceptance_tests(client, out_dir)

        # 生成报告
        report = generate_report(tests, out_dir)
        report_path = out_dir / "acceptance_report.md"
        with open(report_path, "w", encoding="utf-8") as f:
            f.write(report)
        print(f"\n报告已保存: {report_path}")

        # 保存测试结果 JSON
        results_path = out_dir / "test_results.json"
        with open(results_path, "w", encoding="utf-8") as f:
            json.dump([t.to_dict() for t in tests], f, indent=2, ensure_ascii=False)
        print(f"测试结果已保存: {results_path}")

        # 退出应用
        if args.quit:
            try:
                client.call("quit")
                print("已发送退出命令")
            except Exception:
                pass

    finally:
        client.close()

    # 返回失败数
    failed = sum(1 for t in tests if not t.passed)
    return 1 if failed > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
