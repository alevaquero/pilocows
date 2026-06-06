use axum::{
    extract::{Query, State},
    Json,
};
use sqlx::SqlitePool;

use crate::{
    error::Result,
    models::{HerdReportQuery, HerdReportRow},
};

const HERD_SELECT: &str = r#"
    SELECT
        a.id,
        t.tag_number,
        a.breed,
        a.category,
        a.sex,
        a.dob,
        a.is_active,
        (SELECT weight_kg  FROM weights WHERE animal_id = a.id ORDER BY weighed_at DESC LIMIT 1)          AS last_weight_kg,
        (SELECT weighed_at FROM weights WHERE animal_id = a.id ORDER BY weighed_at DESC LIMIT 1)          AS last_weighed_at,
        (SELECT weight_kg  FROM weights WHERE animal_id = a.id ORDER BY weighed_at DESC LIMIT 1 OFFSET 1) AS prev_weight_kg,
        (SELECT weighed_at FROM weights WHERE animal_id = a.id ORDER BY weighed_at DESC LIMIT 1 OFFSET 1) AS prev_weighed_at,
        (SELECT result     FROM pregnancies WHERE animal_id = a.id ORDER BY checked_at DESC LIMIT 1)      AS last_pregnancy_result,
        (SELECT checked_at FROM pregnancies WHERE animal_id = a.id ORDER BY checked_at DESC LIMIT 1)      AS last_pregnancy_checked_at,
        (SELECT due_date   FROM pregnancies WHERE animal_id = a.id ORDER BY checked_at DESC LIMIT 1)      AS last_pregnancy_due_date,
        (SELECT test_name  FROM tests WHERE animal_id = a.id ORDER BY tested_at DESC LIMIT 1)             AS last_test_name,
        (SELECT result     FROM tests WHERE animal_id = a.id ORDER BY tested_at DESC LIMIT 1)             AS last_test_result,
        (SELECT tested_at  FROM tests WHERE animal_id = a.id ORDER BY tested_at DESC LIMIT 1)             AS last_tested_at,
        (SELECT vaccine    FROM vaccinations WHERE animal_id = a.id AND next_due_at IS NOT NULL ORDER BY next_due_at ASC LIMIT 1)    AS next_vaccine,
        (SELECT next_due_at FROM vaccinations WHERE animal_id = a.id AND next_due_at IS NOT NULL ORDER BY next_due_at ASC LIMIT 1)   AS next_vaccine_due_at,
        (SELECT COUNT(*)   FROM vaccinations WHERE animal_id = a.id AND next_due_at IS NOT NULL AND next_due_at < date('now'))       AS overdue_count,
        (SELECT reason     FROM removals WHERE animal_id = a.id) AS removal_reason,
        (SELECT removed_at FROM removals WHERE animal_id = a.id) AS removal_date
    FROM animals a
    JOIN tags t ON t.id = a.tag_id
    WHERE 1=1"#;

pub async fn herd_report(
    State(pool): State<SqlitePool>,
    Query(q): Query<HerdReportQuery>,
) -> Result<Json<Vec<HerdReportRow>>> {
    let mut sql = String::from(HERD_SELECT);

    if !q.include_inactive.unwrap_or(false) {
        sql.push_str(" AND a.is_active = 1");
    }
    if q.category.is_some() {
        sql.push_str(" AND a.category = ?");
    }
    sql.push_str(" ORDER BY t.tag_number");

    let mut query = sqlx::query_as::<_, HerdReportRow>(&sql);
    if let Some(ref cat) = q.category {
        query = query.bind(cat);
    }

    let rows = query.fetch_all(&pool).await?;
    Ok(Json(rows))
}
