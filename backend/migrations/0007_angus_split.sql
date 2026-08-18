-- Split the "Angus" breed into "Angus Red" and "Angus Black" — the frontend
-- BREEDS list no longer offers plain "Angus". Existing animals recorded as
-- "Angus" become "Angus Red" (the more common variety), per farm records.

UPDATE animals SET breed = 'Angus Red' WHERE breed = 'Angus';
