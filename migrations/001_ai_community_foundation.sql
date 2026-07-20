-- Stage 0: AI community foundation.
-- MySQL 8.0+. This migration is additive and safe to execute repeatedly.
-- It intentionally does not alter or delete existing user/user_posts data.

CREATE TABLE IF NOT EXISTS schema_migrations (
    version VARCHAR(64) PRIMARY KEY,
    description VARCHAR(255) NOT NULL,
    applied_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS ai_content_registry (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    source_type VARCHAR(32) NOT NULL,
    source_id VARCHAR(255) NOT NULL,
    author VARCHAR(255) NULL,
    title VARCHAR(500) NULL,
    trust_level VARCHAR(32) NOT NULL DEFAULT 'community_unverified',
    index_status VARCHAR(32) NOT NULL DEFAULT 'pending',
    search_enabled TINYINT(1) NOT NULL DEFAULT 1,
    rag_enabled TINYINT(1) NOT NULL DEFAULT 0,
    content_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
    embedding_model VARCHAR(128) NULL,
    embedding_dimension INT UNSIGNED NULL,
    source_created_at TIMESTAMP NULL,
    indexed_at TIMESTAMP NULL,
    last_error VARCHAR(1000) NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_ai_content_source (source_type, source_id),
    KEY idx_ai_content_status (index_status, updated_at),
    KEY idx_ai_content_search (source_type, search_enabled, index_status),
    KEY idx_ai_content_rag (rag_enabled, trust_level, index_status),
    CONSTRAINT chk_ai_content_source_type
        CHECK (source_type IN ('knowledge', 'community')),
    CONSTRAINT chk_ai_content_trust
        CHECK (trust_level IN ('curated_knowledge', 'community_verified', 'community_unverified')),
    CONSTRAINT chk_ai_content_status
        CHECK (index_status IN ('pending', 'processing', 'ready', 'failed', 'blocked', 'deleted'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS community_actions (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    event_id CHAR(36) NOT NULL,
    username VARCHAR(255) NOT NULL,
    post_id BIGINT UNSIGNED NOT NULL,
    action_type VARCHAR(32) NOT NULL,
    duration_ms INT UNSIGNED NOT NULL DEFAULT 0,
    recommendation_request_id VARCHAR(64) NULL,
    position SMALLINT UNSIGNED NULL,
    occurred_at TIMESTAMP NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_community_action_event (event_id),
    KEY idx_community_action_user_time (username, created_at),
    KEY idx_community_action_post_type (post_id, action_type, created_at),
    KEY idx_community_action_request (recommendation_request_id),
    CONSTRAINT chk_community_action_type
        CHECK (action_type IN ('impression', 'open', 'dwell', 'like', 'unlike',
                               'collect', 'uncollect', 'skip', 'dislike')),
    CONSTRAINT chk_community_action_duration CHECK (duration_ms <= 1800000)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS community_post_stats (
    post_id BIGINT UNSIGNED NOT NULL,
    impressions BIGINT UNSIGNED NOT NULL DEFAULT 0,
    opens BIGINT UNSIGNED NOT NULL DEFAULT 0,
    dwell_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,
    likes BIGINT UNSIGNED NOT NULL DEFAULT 0,
    collects BIGINT UNSIGNED NOT NULL DEFAULT 0,
    skips BIGINT UNSIGNED NOT NULL DEFAULT 0,
    dislikes BIGINT UNSIGNED NOT NULL DEFAULT 0,
    hot_score DOUBLE NOT NULL DEFAULT 0,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (post_id),
    KEY idx_community_stats_hot (hot_score, updated_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS community_user_post_state (
    username VARCHAR(255) NOT NULL,
    post_id BIGINT UNSIGNED NOT NULL,
    liked TINYINT(1) NOT NULL DEFAULT 0,
    collected TINYINT(1) NOT NULL DEFAULT 0,
    disliked TINYINT(1) NOT NULL DEFAULT 0,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (username, post_id),
    KEY idx_community_state_post (post_id),
    KEY idx_community_state_user_disliked (username, disliked, updated_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS user_interest_profiles (
    username VARCHAR(255) NOT NULL,
    profile_version BIGINT UNSIGNED NOT NULL DEFAULT 1,
    embedding_model VARCHAR(128) NULL,
    embedding_dimension INT UNSIGNED NULL,
    embedding_json JSON NULL,
    interest_summary TEXT NULL,
    last_event_id BIGINT UNSIGNED NULL,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (username),
    KEY idx_interest_profile_updated (updated_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT INTO schema_migrations(version, description)
VALUES ('001_ai_community_foundation', 'AI community registry, actions, stats and profiles')
ON DUPLICATE KEY UPDATE description = VALUES(description);
