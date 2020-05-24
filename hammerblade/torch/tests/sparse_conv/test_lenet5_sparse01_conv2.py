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

def test_lenet5_sparse01_conv2():

    di = torch.rand(1, 6, 14, 14)
    dw = torch.rand(16, 6, 5, 5)
    dw = m01(dw)
    sw = dw.to_sparse()

    cpu_i = convert_dense_input(di, 5, 5)
    cpu_sw = dw.view(16, -1).to_sparse()
    cpu_out = torch.sparse.mm(cpu_sw, cpu_i)
    out1 = cpu_out.view(1, 16, 10, 10)

    hb_i = di.hammerblade()
    hb_sw = sw.hammerblade()
    hb_out = F.conv2d(hb_i, hb_sw, bias = None, stride = 1, padding = 0, dilation = 1)
    out2 = hb_out.cpu()

    assert torch.allclose(out1, out2)
