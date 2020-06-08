#ifndef INCLUDED_SCENARIO_EXPRESSION_EXPRESSION_H
#define INCLUDED_SCENARIO_EXPRESSION_EXPRESSION_H

#include <boost/smart_ptr/shared_ptr.hpp>
#include <functional>
#include <iostream>
#include <pluginlib/class_loader.h>
#include <ros/ros.h>
#include <scenario_conditions/condition_base.h>
#include <type_traits>
#include <utility>
#include <yaml-cpp/yaml.h>

namespace scenario_expression
{

/* -----------------------------------------------------------------------------
 *
 * EXPRESSION
 *   <Expression> = <Literal>
 *                | <Logical>
 *                | <Procedure Call>
 *                | <Sequential>
 *                | <Parallel>
 *
 * LITERAL EXPRESSION
 *   <Literal> = <Boolean> | <Number>
 *
 *   <Number> = <Double Float>
 *
 * LOGICAL EXPRESSION
 *   <Logical> = <Logical Operator> [ <Test>* ]
 *
 *   <Logical Operator> = <And> | <Or> | <Not>
 *
 *   <Test> = <Expression>
 *
 * PROCEDURE CALL
 *   <Procedure Call> = <Action Call> | <Predicate Call>
 *
 * SEQUENTIAL EXPRESSION
 *   <Sequential>
 *
 * PARALLEL EXPRESSION
 *   <Parallel>
 *
 * The value of the test is Boolean, which returns whether the return value of
 * the expression is equal to false or not. Note that the return value of the
 * expression is not necessarily Boolean.
 *
 * -------------------------------------------------------------------------- */

template <template <typename T> typename Comparator>
class Logical;

using And = Logical<std::logical_and>;
using Or  = Logical<std::logical_or>;

class Expression
{
  friend And;
  friend Or;

public:
  Expression()
    : data { nullptr }
    , reference_count { 0 }
  {}

  Expression(const Expression& expression)
    : data { expression.data }
    , reference_count { 0 }
  {
    if (data)
    {
      ++(data->reference_count);
    }
  }

  virtual ~Expression()
  {
    if (data && --(data->reference_count) == 0)
    {
      std::cout << "Expression::~Expression deleting data" << std::endl;
      delete data;
    }
  }

  Expression& operator =(const Expression& rhs)
  {
    Expression e { rhs };
    swap(e);
    return *this;
  }

  virtual Expression evaluate()
  {}

  template <typename T, typename... Ts>
  static auto make(Ts&&... xs)
  {
    Expression e {};
    e.remake(new T { std::forward<decltype(xs)>(xs)... });
    return e;
  }

  void swap(Expression& e) noexcept
  {
    std::swap(data, e.data);
  }

protected:
  Expression(std::integral_constant<decltype(0), 0>)
    : data { nullptr }
    , reference_count { 1 }
  {}

private:
  void remake(Expression* e)
  {
    if (data && --(data->reference_count) == 0)
    {
      delete data;
    }

    data = e;
  }

  Expression* data;

  std::size_t reference_count;
};

Expression read(const YAML::Node&);

template <typename T>
class Literal
  : public Expression
{
  friend Expression;

  using value_type = T;

  value_type value;

protected:
  Literal(const Literal& rhs)
    : Expression { std::integral_constant<decltype(0), 0>() }
    , value { rhs.value }
  {}

  Literal(const YAML::Node& node)
    : Expression { std::integral_constant<decltype(0), 0>() }
  {
  }

  virtual ~Literal() = default;

  value_type evaluate() const noexcept override
  {
    return value;
  }
};

template <template <typename T> typename Comparator>
class Logical
  : public Expression
{
  friend Expression;

  Comparator<bool> compare;

  std::vector<Expression> operands;

  std::size_t arity;

protected:
  Logical(const Logical& operation)
    : Expression { std::integral_constant<decltype(0), 0>() }
    , compare { operation.compare }
    , operands { operation.operands }
  {}

  Logical(const YAML::Node& operands_node)
    : Expression { std::integral_constant<decltype(0), 0>() }
  {
    std::cout << "\e[1;31m(logical";

    if (operands_node.IsSequence())
    {
      for (const auto& each : operands_node)
      {
        std::cout << " ";
        operands.push_back(read(each));
      }
    }

    std::cout << "\e[1;31m)";
  }

  virtual ~Logical() = default;

  // virtual operator bool()
  // {
  //   return evaluate();
  // }

  virtual Expression evaluate()
  {
  }
};

template <typename PluginBase>
class Procedure
  : public Expression
{
  friend Expression;

protected:
  Procedure()
    : Expression { std::integral_constant<decltype(0), 0>() }
  {}

  Procedure(const Procedure& proc)
    : Expression { std::integral_constant<decltype(0), 0>() }
  {}

  Procedure(const YAML::Node& node)
    : Expression { std::integral_constant<decltype(0), 0>() }
  {}

  boost::shared_ptr<PluginBase> call;

  virtual pluginlib::ClassLoader<PluginBase>& loader() const = 0;

  const auto& declarations()
  {
    static const auto result { loader().getDeclaredClasses() };
    return result;
  }

  auto load(const std::string& name)
  {
    for (const auto& declaration : declarations())
    {
      if (declaration == name)
      {
        return loader().createInstance(declaration);
      }
    }

    ROS_ERROR_STREAM(__FILE__ << ":" << __LINE__ << ": Failed to load Predicate " << name);
    return boost::shared_ptr<PluginBase>(nullptr);
  }
};

class Predicate
  : public Procedure<scenario_conditions::ConditionBase>
{
  friend Procedure;

protected:
  using Procedure::Procedure;

  pluginlib::ClassLoader<scenario_conditions::ConditionBase>& loader() const override
  {
    static pluginlib::ClassLoader<scenario_conditions::ConditionBase> loader {
      "scenario_conditions", "scenario_conditions::ConditionBase"
    };
    return loader;
  }
};

// TODO MOVE INTO Expression's CONSTRUCTOR!
Expression read(const YAML::Node& node)
{
  if (node.IsScalar())
  {
    ROS_ERROR_STREAM("IsScalar " << node);
  }
  else if (node.IsSequence())
  {
    ROS_ERROR_STREAM("IsSequence " << node);
  }
  else if (node.IsMap()) // NOTE: is keyword
  {
    if (const auto node_and { node["All"] }) // NOTE: should be 'and'
    {
      return Expression::make<And>(node_and);
    }
    else if (const auto node_or { node["Any"] }) // NOTE: should be 'or'
    {
      return Expression::make<Or>(node_or);
    }
    else if (const auto node_type { node["Type"] }) // <procedure call>
    {
      if (const auto node_params { node["Params"] }) // <action call>
      {
        std::cout << "\e[1;31m(change " << node_type.as<std::string>() << ")";
      }
      else // <predicate call>
      {
        std::cout << "\e[1;31m(if " << node_type.as<std::string>() << ")";
        return Expression::make<Predicate>(node);
      }
    }
    else
    {
      std::cout << "ERROR!";
    }
  }

  return {};
}

} // namespace scenario_expression

namespace std
{

template <>
void swap(scenario_expression::Expression& lhs,
          scenario_expression::Expression& rhs)
{
  lhs.swap(rhs);
}

} // namespace std

#endif // INCLUDED_SCENARIO_EXPRESSION_EXPRESSION_H
