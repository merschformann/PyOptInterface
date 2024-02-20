import pyoptinterface as poi
from pytest import approx
import pytest


def test_gurobi_lazyupdate(model_interface):
    model = model_interface

    name = model.get_model_attribute(poi.ModelAttribute.SolverName)
    if name.lower() != "gurobi":
        pytest.skip("This test is only for Gurobi")

    x = model.add_variables(range(3), lb=1.0, ub=2.0)

    model.delete_variable(x[1])

    obj = x[0] - x[2]
    model.set_objective(obj)

    model.optimize()

    assert model.get_value(x[0]) == approx(1.0)
    assert model.get_value(x[2]) == approx(2.0)

    model.set_variable_attribute(x[0], poi.VariableAttribute.LowerBound, 2.0)
    assert model.get_variable_attribute(
        x[0], poi.VariableAttribute.LowerBound
    ) == approx(2.0)

    model.optimize()

    assert model.get_value(x[0]) == approx(2.0)
    assert model.get_value(x[2]) == approx(2.0)
