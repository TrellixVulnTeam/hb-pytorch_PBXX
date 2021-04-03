"""
Tests on torch.nn.relu (threshold kernel)
03/09/2020 Lin Cheng (lc873@cornell.edu)
"""
import torch
import torch.nn as nn
from hypothesis import given, settings
from .hypothesis_test_util import HypothesisUtil as hu

def test_torch_nn_sigmoid_1():
    sigmoid = nn.Sigmoid()
    x = torch.ones(10)
    x_h = x.hammerblade()
    x_sig = sigmoid(x)
    x_h_sig = sigmoid(x_h)
    assert x_h_sig.device == torch.device("hammerblade")
    assert torch.allclose(x_h_sig.cpu(), x_sig)

def test_torch_nn_sigmoid_2():
    sigmoid = nn.Sigmoid()
    x = torch.randn(10)
    x_h = x.hammerblade()
    x_sig = sigmoid(x)
    x_h_sig = sigmoid(x_h)
    assert x_h_sig.device == torch.device("hammerblade")
    assert torch.allclose(x_h_sig.cpu(), x_sig)

def test_torch_nn_sigmoid_3():
    sigmoid = nn.Sigmoid()
    x = torch.randn(3, 4)
    x_h = x.hammerblade()
    x_sig = sigmoid(x)
    x_h_sig = sigmoid(x_h)
    assert x_h_sig.device == torch.device("hammerblade")
    assert torch.allclose(x_h_sig.cpu(), x_sig)

def _test_torch_sigmoid_check(tensor_self):
    tensor_self_hb = torch.tensor(tensor_self).hammerblade()
    result_hb = torch.sigmoid(tensor_self_hb)
    assert result_hb.device == torch.device("hammerblade")
    assert torch.allclose(result_hb.cpu(), torch.sigmoid(torch.tensor(tensor_self)))

@settings(deadline=None)
@given(tensor=hu.tensor())
def test_elementwise_torch_sigmoid_hypothesis(tensor):
    _test_torch_sigmoid_check(tensor)
