-- Stage 2: keep the community index registry synchronized with post lifecycle.
-- MySQL 8.0+. Existing post data is preserved.

INSERT INTO ai_content_registry
    (source_type, source_id, author, trust_level, index_status,
     search_enabled, rag_enabled, content_version, source_created_at)
SELECT 'community', CAST(p.id AS CHAR), p.username, 'community_unverified',
       'pending', 1, 0, 1, p.created_at
FROM user_posts p
ON DUPLICATE KEY UPDATE author = VALUES(author);

DROP TRIGGER IF EXISTS trg_user_posts_ai_insert;
CREATE TRIGGER trg_user_posts_ai_insert
AFTER INSERT ON user_posts
FOR EACH ROW
INSERT INTO ai_content_registry
    (source_type, source_id, author, trust_level, index_status,
     search_enabled, rag_enabled, content_version, source_created_at)
VALUES
    ('community', CAST(NEW.id AS CHAR), NEW.username, 'community_unverified',
     'pending', 1, 0, 1, NEW.created_at)
ON DUPLICATE KEY UPDATE
    author = VALUES(author), index_status = 'pending', search_enabled = 1,
    last_error = NULL;

DROP TRIGGER IF EXISTS trg_user_posts_ai_update;
CREATE TRIGGER trg_user_posts_ai_update
AFTER UPDATE ON user_posts
FOR EACH ROW
INSERT INTO ai_content_registry
    (source_type, source_id, author, trust_level, index_status,
     search_enabled, rag_enabled, content_version, source_created_at)
VALUES
    ('community', CAST(NEW.id AS CHAR), NEW.username, 'community_unverified',
     'pending', 1, 0, 1, NEW.created_at)
ON DUPLICATE KEY UPDATE
    author = VALUES(author),
    index_status = IF(index_status IN ('blocked', 'deleted'), index_status, 'pending'),
    content_version = content_version + 1,
    last_error = NULL;

DROP TRIGGER IF EXISTS trg_user_posts_ai_delete;
CREATE TRIGGER trg_user_posts_ai_delete
AFTER DELETE ON user_posts
FOR EACH ROW
UPDATE ai_content_registry
SET index_status = 'deleted', search_enabled = 0, rag_enabled = 0,
    content_version = content_version + 1, last_error = NULL
WHERE source_type = 'community' AND CAST(source_id AS UNSIGNED) = OLD.id;

INSERT INTO schema_migrations(version, description)
VALUES ('002_community_index_lifecycle', 'Backfill posts and track update/delete lifecycle')
ON DUPLICATE KEY UPDATE description = VALUES(description);
