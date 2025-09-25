#!/bin/bash

# Redis源码解析测试脚本
# 运行所有单元测试和集成测试

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 测试统计
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# 日志函数
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 运行单个测试
run_test() {
    local test_file=$1
    local test_name=$(basename "$test_file" .c)

    log_info "Running test: $test_name"

    # 编译测试
    if gcc -o "test_${test_name}" "$test_file" -O2 -Wall -Wextra 2>/dev/null; then
        TOTAL_TESTS=$((TOTAL_TESTS + 1))

        # 运行测试
        if "./test_${test_name}" 2>/dev/null; then
            log_success "Test $test_name passed"
            PASSED_TESTS=$((PASSED_TESTS + 1))
        else
            log_error "Test $test_name failed"
            FAILED_TESTS=$((FAILED_TESTS + 1))
        fi

        # 清理
        rm -f "test_${test_name}"
    else
        log_error "Failed to compile $test_name"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
}

# 运行内存检查
run_valgrind_test() {
    local test_file=$1
    local test_name=$(basename "$test_file" .c)

    log_info "Running Valgrind test: $test_name"

    # 编译测试
    if gcc -g -o "test_${test_name}" "$test_file" 2>/dev/null; then
        TOTAL_TESTS=$((TOTAL_TESTS + 1))

        # 运行Valgrind
        if valgrind --leak-check=full --error-exitcode=1 "./test_${test_name}" 2>/dev/null; then
            log_success "Valgrind test $test_name passed"
            PASSED_TESTS=$((PASSED_TESTS + 1))
        else
            log_error "Valgrind test $test_name failed"
            FAILED_TESTS=$((FAILED_TESTS + 1))
        fi

        # 清理
        rm -f "test_${test_name}"
    else
        log_error "Failed to compile $test_name for Valgrind"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
}

# 运行性能测试
run_performance_test() {
    local test_file=$1
    local test_name=$(basename "$test_file" .c)

    log_info "Running performance test: $test_name"

    # 编译测试
    if gcc -O3 -o "perf_${test_name}" "$test_file" 2>/dev/null; then
        TOTAL_TESTS=$((TOTAL_TESTS + 1))

        # 运行性能测试
        local start_time=$(date +%s.%N)
        if "./perf_${test_name}" 2>/dev/null; then
            local end_time=$(date +%s.%N)
            local duration=$(echo "$end_time - $start_time" | bc)
            log_success "Performance test $test_name completed in ${duration}s"
            PASSED_TESTS=$((PASSED_TESTS + 1))
        else
            log_error "Performance test $test_name failed"
            FAILED_TESTS=$((FAILED_TESTS + 1))
        fi

        # 清理
        rm -f "perf_${test_name}"
    else
        log_error "Failed to compile $test_name for performance test"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
}

# 检查依赖
check_dependencies() {
    log_info "Checking dependencies..."

    # 检查编译器
    if ! command -v gcc &> /dev/null; then
        log_error "GCC compiler not found. Please install gcc."
        exit 1
    fi

    # 检查Valgrind（可选）
    if ! command -v valgrind &> /dev/null; then
        log_warning "Valgrind not found. Memory leak tests will be skipped."
    fi

    # 检查bc（用于性能测试）
    if ! command -v bc &> /dev/null; then
        log_warning "bc not found. Performance timing may not work."
    fi

    log_success "Dependencies check completed"
}

# 清理函数
cleanup() {
    log_info "Cleaning up test files..."
    rm -f test_* perf_*
    log_success "Cleanup completed"
}

# 显示结果
show_results() {
    echo ""
    echo "=========================================="
    echo "           Test Results Summary"
    echo "=========================================="
    echo -e "Total Tests:   ${BLUE}$TOTAL_TESTS${NC}"
    echo -e "Passed Tests:  ${GREEN}$PASSED_TESTS${NC}"
    echo -e "Failed Tests:  ${RED}$FAILED_TESTS${NC}"
    echo "=========================================="

    if [ $FAILED_TESTS -eq 0 ]; then
        echo -e "${GREEN}🎉 All tests passed successfully!${NC}"
        exit 0
    else
        echo -e "${RED}❌ $FAILED_TESTS test(s) failed!${NC}"
        exit 1
    fi
}

# 主函数
main() {
    echo "🚀 Redis Source Code Analysis Test Suite"
    echo "=========================================="
    echo ""

    # 检查依赖
    check_dependencies

    # 设置trap确保清理
    trap cleanup EXIT

    # 切换到测试目录
    cd "$(dirname "$0")"

    # 查找所有测试文件
    local test_files=()
    for file in test_*.c; do
        if [ -f "$file" ]; then
            test_files+=("$file")
        fi
    done

    if [ ${#test_files[@]} -eq 0 ]; then
        log_error "No test files found!"
        exit 1
    fi

    log_info "Found ${#test_files[@]} test files"
    echo ""

    # 运行基础测试
    log_info "Running basic tests..."
    echo ""
    for test_file in "${test_files[@]}"; do
        run_test "$test_file"
    done

    echo ""

    # 运行Valgrind测试（如果可用）
    if command -v valgrind &> /dev/null; then
        log_info "Running memory leak tests with Valgrind..."
        echo ""
        for test_file in "${test_files[@]}"; do
            run_valgrind_test "$test_file"
        done
        echo ""
    fi

    # 运行性能测试
    log_info "Running performance tests..."
    echo ""
    for test_file in "${test_files[@]}"; do
        run_performance_test "$test_file"
    done

    echo ""

    # 显示结果
    show_results
}

# 如果直接运行此脚本
if [ "${BASH_SOURCE[0]}" == "${0}" ]; then
    main "$@"
fi