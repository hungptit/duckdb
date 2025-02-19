#include "duckdb/planner/expression/bound_window_expression.hpp"

#include "duckdb/common/string_util.hpp"
#include "duckdb/function/aggregate_function.hpp"

namespace duckdb {

BoundWindowExpression::BoundWindowExpression(ExpressionType type, LogicalType return_type,
                                             unique_ptr<AggregateFunction> aggregate,
                                             unique_ptr<FunctionData> bind_info)
    : Expression(type, ExpressionClass::BOUND_WINDOW, move(return_type)), aggregate(move(aggregate)),
      bind_info(move(bind_info)), ignore_nulls(false) {
}

string BoundWindowExpression::ToString() const {
	string result = aggregate.get() ? aggregate->name : ExpressionTypeToString(type);
	result += "(";
	result += StringUtil::Join(children, children.size(), ", ",
	                           [](const unique_ptr<Expression> &child) { return child->GetName(); });
	// Lead/Lag extra arguments
	if (offset_expr.get()) {
		result += ", ";
		result += offset_expr->GetName();
	}
	if (default_expr.get()) {
		result += ", ";
		result += default_expr->GetName();
	}

	// IGNORE NULLS
	if (ignore_nulls) {
		result += " IGNORE NULLS";
	}

	// Over clause
	result += ") OVER(";
	string sep;

	// Partitions
	if (!partitions.empty()) {
		result += "PARTITION BY ";
		result += StringUtil::Join(partitions, partitions.size(), ", ",
		                           [](const unique_ptr<Expression> &partition) { return partition->GetName(); });
		sep = " ";
	}

	// Orders
	if (!orders.empty()) {
		result += sep;
		result += "ORDER BY ";
		result += StringUtil::Join(orders, orders.size(), ", ", [](const BoundOrderByNode &order) {
			auto str = order.expression->GetName();
			str += (order.type == OrderType::ASCENDING) ? " ASC" : " DESC";
			switch (order.null_order) {
			case OrderByNullType::NULLS_FIRST:
				str += " NULLS FIRST";
				break;
			case OrderByNullType::NULLS_LAST:
				str += " NULLS LAST";
				break;
			default:
				break;
			}
			return str;
		});
		sep = " ";
	}

	// Rows/Range
	string units = "ROWS";
	string from;
	switch (start) {
	case WindowBoundary::CURRENT_ROW_RANGE:
	case WindowBoundary::CURRENT_ROW_ROWS:
		from = "CURRENT ROW";
		units = (start == WindowBoundary::CURRENT_ROW_RANGE) ? "RANGE" : "ROWS";
		break;
	case WindowBoundary::UNBOUNDED_PRECEDING:
		if (end != WindowBoundary::CURRENT_ROW_RANGE) {
			from = "UNBOUNDED PRECEDING";
		}
		break;
	case WindowBoundary::EXPR_PRECEDING_ROWS:
	case WindowBoundary::EXPR_PRECEDING_RANGE:
		from = start_expr->GetName() + " PRECEDING";
		units = (start == WindowBoundary::EXPR_PRECEDING_RANGE) ? "RANGE" : "ROWS";
		break;
	case WindowBoundary::EXPR_FOLLOWING_ROWS:
	case WindowBoundary::EXPR_FOLLOWING_RANGE:
		from = start_expr->GetName() + " FOLLOWING";
		units = (start == WindowBoundary::EXPR_FOLLOWING_RANGE) ? "RANGE" : "ROWS";
		break;
	default:
		break;
	}

	string to;
	switch (end) {
	case WindowBoundary::CURRENT_ROW_RANGE:
		if (start != WindowBoundary::UNBOUNDED_PRECEDING) {
			to = "CURRENT ROW";
			units = "RANGE";
		}
		break;
	case WindowBoundary::CURRENT_ROW_ROWS:
		to = "CURRENT ROW";
		units = "ROWS";
		break;
	case WindowBoundary::UNBOUNDED_PRECEDING:
		to = "UNBOUNDED PRECEDING";
		break;
	case WindowBoundary::EXPR_PRECEDING_ROWS:
	case WindowBoundary::EXPR_PRECEDING_RANGE:
		to = end_expr->GetName() + " PRECEDING";
		units = (start == WindowBoundary::EXPR_PRECEDING_RANGE) ? "RANGE" : "ROWS";
		break;
	case WindowBoundary::EXPR_FOLLOWING_ROWS:
	case WindowBoundary::EXPR_FOLLOWING_RANGE:
		to = end_expr->GetName() + " FOLLOWING";
		units = (start == WindowBoundary::EXPR_FOLLOWING_RANGE) ? "RANGE" : "ROWS";
		break;
	default:
		break;
	}

	if (!from.empty() || !to.empty()) {
		result += sep + units;
	}
	if (!from.empty() && !to.empty()) {
		result += " BETWEEN ";
		result += from;
		result += " AND ";
		result += to;
	} else if (!from.empty()) {
		result += " ";
		result += from;
	} else if (!to.empty()) {
		result += " ";
		result += to;
	}

	result += ")";
	return result;
}

bool BoundWindowExpression::Equals(const BaseExpression *other_p) const {
	if (!Expression::Equals(other_p)) {
		return false;
	}
	auto other = (BoundWindowExpression *)other_p;

	if (ignore_nulls != other->ignore_nulls) {
		return false;
	}
	if (start != other->start || end != other->end) {
		return false;
	}
	// check if the child expressions are equivalent
	if (other->children.size() != children.size()) {
		return false;
	}
	for (idx_t i = 0; i < children.size(); i++) {
		if (!Expression::Equals(children[i].get(), other->children[i].get())) {
			return false;
		}
	}
	// check if the framing expressions are equivalent
	if (!Expression::Equals(start_expr.get(), other->start_expr.get()) ||
	    !Expression::Equals(end_expr.get(), other->end_expr.get()) ||
	    !Expression::Equals(offset_expr.get(), other->offset_expr.get()) ||
	    !Expression::Equals(default_expr.get(), other->default_expr.get())) {
		return false;
	}

	return KeysAreCompatible(other);
}

bool BoundWindowExpression::KeysAreCompatible(const BoundWindowExpression *other) const {
	// check if the partitions are equivalent
	if (partitions.size() != other->partitions.size()) {
		return false;
	}
	for (idx_t i = 0; i < partitions.size(); i++) {
		if (!Expression::Equals(partitions[i].get(), other->partitions[i].get())) {
			return false;
		}
	}
	// check if the orderings are equivalent
	if (orders.size() != other->orders.size()) {
		return false;
	}
	for (idx_t i = 0; i < orders.size(); i++) {
		if (orders[i].type != other->orders[i].type) {
			return false;
		}
		if (!BaseExpression::Equals((BaseExpression *)orders[i].expression.get(),
		                            (BaseExpression *)other->orders[i].expression.get())) {
			return false;
		}
	}
	return true;
}

unique_ptr<Expression> BoundWindowExpression::Copy() {
	auto new_window = make_unique<BoundWindowExpression>(type, return_type, nullptr, nullptr);
	new_window->CopyProperties(*this);

	if (aggregate) {
		new_window->aggregate = make_unique<AggregateFunction>(*aggregate);
	}
	if (bind_info) {
		new_window->bind_info = bind_info->Copy();
	}
	for (auto &child : children) {
		new_window->children.push_back(child->Copy());
	}
	for (auto &e : partitions) {
		new_window->partitions.push_back(e->Copy());
	}
	for (auto &ps : partitions_stats) {
		if (ps) {
			new_window->partitions_stats.push_back(ps->Copy());
		} else {
			new_window->partitions_stats.push_back(nullptr);
		}
	}
	for (auto &o : orders) {
		new_window->orders.emplace_back(o.type, o.null_order, o.expression->Copy());
	}

	new_window->start = start;
	new_window->end = end;
	new_window->start_expr = start_expr ? start_expr->Copy() : nullptr;
	new_window->end_expr = end_expr ? end_expr->Copy() : nullptr;
	new_window->offset_expr = offset_expr ? offset_expr->Copy() : nullptr;
	new_window->default_expr = default_expr ? default_expr->Copy() : nullptr;
	new_window->ignore_nulls = ignore_nulls;

	return move(new_window);
}

} // namespace duckdb
