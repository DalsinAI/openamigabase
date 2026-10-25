-- The rows of the golden files' fixture project; its tables are made
-- through OpenBase's API in tests/test_golden.c. Money is in pence.
INSERT INTO Customers (id, Name, Town) VALUES
  (1, 'Ann', 'Leeds'), (2, 'Bob', 'York'), (3, 'Cy', 'Leeds'),
  (4, 'Di', NULL), (5, 'O''Brien', 'Hull'), (6, 'Bea', 'York'), (7, 'Ed', 'Leeds');
INSERT INTO Products (id, Name, Price, "Group") VALUES
  (1, 'Hammer', 1250, 'Tools'), (2, 'Nails', 199, 'Tools'), (3, 'Bread', 150, 'Food'),
  (4, 'Saw', 2400, 'Tools'), (5, 'Jam', 325, 'Food');
INSERT INTO Orders (id, customer, Day, Paid, Total) VALUES
  (1, 1, '2025-12-30', 1, 1449), (2, 1, '2026-01-15', 0, 2400), (3, 2, '2026-02-01', 1, 475),
  (4, 3, '2026-03-31', 0, 12597), (5, 6, '2026-04-02', 1, 150), (6, 2, '2025-06-01', 1, 10000);
INSERT INTO "Order Lines" (id, "order", product, Qty) VALUES
  (1, 1, 1, 1), (2, 1, 2, 1), (3, 2, 4, 1), (4, 3, 3, 1), (5, 3, 5, 1),
  (6, 4, 4, 5), (7, 4, 2, 3), (8, 5, 3, 1), (9, 6, 1, 8);
