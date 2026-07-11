# 本地基准与回归工具

引擎的搜索/评估/热路径改动必须用这套工具验证。fixtures 全部本地提供、
**永不提交**（用户数据、masterdata、music metas）。

## 环境

```bash
uv pip install -e .          # 或设置 SEKAI_BENCH_SO_DIR 指向裸 .so 所在目录
export SEKAI_BENCH_MASTERDATA=./haruki-sekai-master/master   # 默认值
export SEKAI_BENCH_MUSICMETAS=../music_metas.json            # 默认值
export SEKAI_BENCH_USERDATA=./collections.suite.json         # 默认值
```

在**仓库根目录**运行（裸 .so 以 `./data` 解析静态数据）。Mongo 导出的
suite（外层是单元素 JSON 列表）会自动解包。活动 ID 从 masterdata 自动
选取最新的马拉松/WL 活动。

## 回归（每次改动必跑）

```bash
# 改动前，用旧代码构建产物固化基线
python tools/bench/regress.py run baseline.json
# 改动后
python tools/bench/regress.py run after.json
python tools/bench/regress.py compare baseline.json after.json   # 期望 ALL-OK
```

判定规则：
- **确定性场景**（固定种子 GA、能跑完的 DFS）必须逐位一致（EXACT-MATCH）。
- **时间预算场景**（DFS_GA / SA / 含 DFS 预热的挑战 GA）内部有墙钟预算，
  代码变快后探索量合法变化，只比较 top 值不劣化（SANITY-OK）。

## RL 质量协议（改动 RL 或其依赖路径时必跑）

方法学来自提交 a3e5bcb：

```bash
DECK_RL_SEED_CACHE_DISABLE=1 python tools/bench/rl_quality.py --ref --calls 2       # 参考最优（宽预算GA）
DECK_RL_SEED_CACHE_DISABLE=1 python tools/bench/rl_quality.py --processes 4 --calls 5
```

门槛：所有调用的 top 值持续命中参考最优（例如 20/20）。注意：
- **RL 各阶段预算常数是质量下限**——历史上多次为修质量问题而加预算，
  永远不要为提速削减它们；提速的正确方式是降低预算内的实现开销。
- 冷启动的**同分卡组组成不保证确定**（墙钟截断所致），比值不比卡组列表。

## 延迟矩阵（新旧对比）

```bash
# 旧版：从改动前提交建 worktree 编译
git worktree add /tmp/old_src <旧提交> --detach
cmake -S /tmp/old_src -B /tmp/build_old -DCMAKE_BUILD_TYPE=Release ... && cmake --build /tmp/build_old
ln -s $PWD/data /tmp/build_old/data

SEKAI_BENCH_SO_DIR=/tmp/build_old python tools/bench/bench_matrix.py all   # 旧
python tools/bench/bench_matrix.py all                                     # 新
```

新旧单元在**同一会话内成对运行**以抵消温度/负载漂移；跨会话的历史数据
只做参考不做结论。

## 性能剖析提示

- `pybind11` 会 strip Release 产物；剖析用 `-DCMAKE_BUILD_TYPE=RelWithDebInfo`
  单独构建。macOS 用 `sample <pid>`；`_xzm_free` 内的 `mach_absolute_time`
  是分配器自身的计时，不是应用时钟。
- 基准里务必复用 `DeckRecommendUserData` 对象——按路径传入会每次重新解析
  suite JSON（~35ms），淹没被测信号。
- 已知保持逐位兼容的历史行为：`member < 5` 时结果卡组用队长补位到 5 张
  （分数按补位后的卡组计算）；修复它属于行为变更，需单独评审。
