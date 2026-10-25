SELECT Customers.Name, Orders.Day FROM Orders RIGHT JOIN Customers ON Customers.id = Orders.customer ORDER BY Customers.Name, Orders.Day
