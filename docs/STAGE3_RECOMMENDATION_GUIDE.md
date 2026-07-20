# 阶段 3：兴趣画像与推荐排序

## 实现内容

- 根据当前点赞/收藏/不感兴趣状态，以及最近 90 天的停留、打开和跳过事件构建兴趣向量。
- 7 天内行为按近期兴趣处理；更早行为以较低权重保留为长期兴趣。
- 候选池最多取 500 条社区内容，在池内融合语义、热门、最新和稳定探索四路信号。
- 无兴趣画像的新用户使用热门、内容质量、新鲜度和探索分排序。
- 社区向量索引缺失或读取失败时，语义得分自动归零，Feed 仍正常返回。
- 明确“不感兴趣”的帖子直接过滤；曝光和跳过次数产生降权。
- 相同内容去重，并避免相同作者连续出现。
- 用户画像保存到 `user_interest_profiles`，用于观察画像版本和维度。

## 默认打分

```text
推荐分 = 语义相似度 × 0.45
       + 互动质量   × 0.20
       + 内容质量   × 0.15
       + 新鲜度     × 0.10
       + 探索分     × 0.10
       - 已看惩罚
       - 快速划过惩罚
```

所有参数均从环境读取：

```dotenv
COMMUNITY_WEIGHT_SEMANTIC="0.45"
COMMUNITY_WEIGHT_INTERACTION="0.20"
COMMUNITY_WEIGHT_QUALITY="0.15"
COMMUNITY_WEIGHT_FRESHNESS="0.10"
COMMUNITY_WEIGHT_EXPLORATION="0.10"
COMMUNITY_SEEN_PENALTY="0.08"
COMMUNITY_SKIP_PENALTY="0.15"
COMMUNITY_FRESHNESS_HALF_LIFE_DAYS="14"
```

修改权重后重启服务即可，无需重新编译或重建向量。

## 行为权重

| 行为 | 兴趣影响 |
|---|---:|
| 收藏 | +5 |
| 点赞 | +3 |
| 有效停留 | 最多 +3 |
| 打开 | +0.5 |
| 快速划过 | -1 |
| 不感兴趣 | -5，并从该用户结果中过滤 |

7 天外但 90 天内的事件乘以 0.25，避免旧兴趣长期支配推荐。

## Feed 模式

- `mode=latest`：沿用 v1 ID 游标，严格按发布时间倒序。
- `mode=for_you`：使用 v2 偏移游标，按照融合分排序。
- 两种游标不能混用，混用返回 HTTP 400。

## 推荐预览

```bash
cd /root/RAGReactor-AICommunity
set -a
source .env
set +a
make preview-recommendations
./tools/preview_recommendations 用户名
```

输出包含帖子 ID、综合分、主要召回来源和作者，可用于调权重和排查冷启动。

## 回退说明

```text
有有效兴趣向量：semantic + popular + latest + explore
无行为画像：      popular + quality + latest + explore（cold_start）
索引不可用：      popular + quality + latest + explore（community_index_unavailable）
```

AI/索引故障不会让社区 Feed 返回 5xx，只有 MySQL 本身不可用时才返回存储错误。

