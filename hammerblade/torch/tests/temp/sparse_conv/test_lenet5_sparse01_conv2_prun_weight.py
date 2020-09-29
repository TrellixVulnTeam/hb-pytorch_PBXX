import torch
import torch.nn.functional as F

m01 = torch.nn.Threshold(0.9, 0)

def convert_dense_input(x, r, s):
    stride = (1, 1)
    kernel_size = (r, s)
    assert x.dim() == 4
    x = x.unfold(2, kernel_size[0], stride[0])
    x = x.unfold(3, kernel_size[1], stride[1])
    x = torch.flatten(x, start_dim = 4)
    x = x.transpose(1, 3).transpose(1, 2)
    x = torch.flatten(x, start_dim = 3)
    x = torch.flatten(x, start_dim = 0, end_dim = 2).t().contiguous()
    return x

def load_conv2_sparse_weight():
    model = torch.load("LeNet_5.prune.conv.fc.pth.tar", map_location='cpu')
    weights = model.get('state_dict')
    conv2_weight = weights.get('conv2.weight').cpu()
    return conv2_weight

def test_lenet5_sparse01_conv2():

    di = torch.rand(1, 20, 12, 12)

    cpu_i = convert_dense_input(di, 5, 5)
    cpu_sw = load_conv2_sparse_weight().view(50, -1).to_sparse()
    cpu_out = torch.sparse.mm(cpu_sw, cpu_i)
    out1 = cpu_out.view(1, 50, 8, 8)
    print(out1)

    hb_i = di.hammerblade()
    hb_sw = load_conv2_sparse_weight().to_sparse().hammerblade()
    hb_out = F.conv2d(hb_i, hb_sw, bias = None, stride = 1, padding = 0, dilation = 1)
    out2 = hb_out.cpu()
    print(out2)

    assert torch.allclose(out1, out2, atol=1e-6)

def test_special_sparse01_conv():

    di = torch.rand(1, 4, 8, 8)
    dw = torch.rand(4, 4, 5, 5)
    dw = m01(dw)
    sw = dw.to_sparse()

    cpu_i = convert_dense_input(di, 5, 5)
    cpu_sw = dw.view(4, -1).to_sparse()
    cpu_out = torch.sparse.mm(cpu_sw, cpu_i)
    out1 = cpu_out.view(1, 4, 4, 4)

    hb_i = di.hammerblade()
    hb_sw = dw.hammerblade()
    hb_out = F.conv2d(hb_i, hb_sw, bias = None, stride = 1, padding = 0, dilation = 1)
    out2 = hb_out.cpu()

    assert torch.allclose(out1, out2)
